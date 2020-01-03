#include "ext2_utils.c"

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *gd;

// Simple singly linked list
struct list {
    struct list *next;
    int val;
};

struct list *inode_list; // Which inodes have already been fixed
struct list *block_list; // Which blocks have been fixed

int fix_entry(struct ext2_dir_entry *entry, unsigned file_type) {
    int fixed = 0;
    struct ext2_inode *inode = inode_by_index(entry->inode);

    if (inode == NULL) return 0;
    if (entry->file_type != file_type) {
        entry->file_type = file_type;
        printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", entry->inode);
        fixed++;
    }
    if (!inode_is_allocated(entry->inode)) {
        realloc_inode(entry->inode);
        printf("Fixed: inode [%d] not marked as in-use\n", entry->inode);
        fixed++;
    }
    if (inode->i_dtime != 0) {
        inode->i_dtime = 0;
        fixed++;
        printf("Fixed: valid inode marked for deletion: [%d]\n", entry->inode);
    }
    int blocks_fixed = 0;
    for (int b = 0; b < inode->i_blocks / 2; b++)
        if (!block_is_allocated(inode->i_block[b])) {
            realloc_block(inode->i_block[b]);
            blocks_fixed++;
        }

    if (blocks_fixed > 0) printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n",
                                 blocks_fixed, entry->inode);
    return fixed + blocks_fixed;
}

int rec_fix_entries(struct ext2_dir_entry *dir) {
    int fixed = 0;
    struct ext2_dir_entry *c = dir;
    int s = 0;
    struct list *tmp; // Buffer for iterating over lists
    struct ext2_inode *inode; // Holds the inode we are currently checking

    while (s < BLOCK_SIZE) {
        s += c->rec_len;
        int already_checked_inode = 0;
        tmp = inode_list;
        while (tmp != NULL) {
            if (tmp->val == c->inode) {
                already_checked_inode = 1;
                break;
            }
            tmp = tmp->next;
        }

        inode = inode_by_index(c->inode);
        if (!already_checked_inode) {
            if (inode->i_mode & EXT2_S_IFDIR) {
                fixed += fix_entry(c, EXT2_FT_DIR);
                for (int b = 0; b < inode->i_blocks / 2; b++) {
                    tmp = block_list;
                    int done = 0;
                    while (tmp->next != NULL) {
                        if (tmp->val == inode->i_block[b]) done = 1;
                        tmp = tmp->next;
                    }
                    if (done) continue;
                    struct list *new_block = malloc(sizeof(struct list));
                    if (new_block == NULL) return 0;
                    new_block->val = inode->i_block[b];
                    new_block->next = NULL;
                    tmp->next = new_block;
                    fixed += rec_fix_entries((struct ext2_dir_entry *) (disk + BLOCK_SIZE * inode->i_block[b]));
                }
            } else if (inode->i_mode & EXT2_S_IFREG) {
                fixed += fix_entry(c, EXT2_FT_REG_FILE);
            } else if (inode->i_mode & EXT2_S_IFLNK) {
                fixed += fix_entry(c, EXT2_FT_SYMLINK);
            }

            struct list *new_inode = malloc(sizeof(struct list));
            if (new_inode == NULL) return 0;
            new_inode->next = inode_list;
            new_inode->val = c->inode;
            inode_list = new_inode;
        }

        c = (struct ext2_dir_entry *) (((char *) c) + c->rec_len);
    }
    return fixed;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    sb = (struct ext2_super_block *) (disk + BLOCK_SIZE);
    gd = (struct ext2_group_desc *) (disk + BLOCK_SIZE + sizeof(struct ext2_super_block));

    int err_count = 0;
    int diff;

    /** Verify free inode counts **/
    int real_free_inodes = 0;
    for (int byte = 0; byte < sb->s_inodes_count; byte++)
        for (int bit = 0; bit < 8; bit++)
            if (!((disk[BLOCK_SIZE * gd->bg_inode_bitmap + byte] >> bit) & 1)) real_free_inodes++;
    if (real_free_inodes != sb->s_free_inodes_count) {
        diff = real_free_inodes - sb->s_free_inodes_count;
        printf("Fixed superblock's free inodes counter was off by %d compared to the bitmap\n", diff);
        sb->s_free_inodes_count = real_free_inodes;
        err_count += diff;
    }
    if (real_free_inodes != gd->bg_free_inodes_count) {
        diff = real_free_inodes - gd->bg_free_inodes_count;
        printf("Fixed block group's free inodes counter was off by %d compared to the bitmap\n", diff);
        gd->bg_free_inodes_count = real_free_inodes;
        err_count += diff;
    }

    /** Verify free block counts **/
    int real_free_blocks = 0;
    for (int byte = 0; byte < sb->s_blocks_count; byte++)
        for (int bit = 0; bit < 8; bit++)
            if (!((disk[BLOCK_SIZE * gd->bg_block_bitmap + byte] >> bit) & 1)) real_free_blocks++;
    if (real_free_blocks != sb->s_free_blocks_count) {
        diff = real_free_blocks - sb->s_free_blocks_count;
        printf("Fixed superblock's free blocks counter was off by %d compared to the bitmap\n", diff);
        sb->s_free_blocks_count = real_free_blocks;
        err_count += diff;
    }
    if (real_free_blocks != gd->bg_free_blocks_count) {
        diff = real_free_blocks - gd->bg_free_blocks_count;
        printf("Fixed block group's free blocks counter was off by %d compared to the bitmap\n", diff);
        gd->bg_free_blocks_count = real_free_blocks;
        err_count += diff;
    }

    struct ext2_inode *inode = (struct ext2_inode *) (disk + BLOCK_SIZE * gd->bg_inode_table + sb->s_inode_size * (EXT2_ROOT_INO - 1));
    block_list = malloc(sizeof(struct list));
    block_list->next = NULL;
    block_list->val = 0; // Ensure first element has value 0 so we don't segfault trying to check invalid blocks
    inode_list = malloc(sizeof(struct list));
    inode_list->next = NULL;
    inode_list->val = 0; // Ensure first element has value 0 so we don't segfault trying to check invalid inodes
    for (int b = 0; b < inode->i_blocks / 2; b++) {
        err_count += rec_fix_entries((struct ext2_dir_entry *) (disk + BLOCK_SIZE * inode->i_block[b]));
    }
    if (err_count == 0) printf("No file system inconsistencies detected!\n");
    else printf("%d file system inconsistencies repaired!\n", err_count);
    return 0;
}
