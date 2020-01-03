#include "ext2_utils.c"

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *gd;

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <absolute path of file/link on virtual disk>\n", argv[0]);
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

    if (get_dir_entry_by_path(argv[2], 0) != NULL) return EEXIST;
    struct dir_name *split = split_path(argv[2]);
    if (split->trailing_slash) return EISDIR; // Can't restore a directory
    if (split->name == NULL) return EINVAL;
    struct ext2_dir_entry *parent = get_dir_entry_by_path(split->parent, 1);
    if (parent == NULL) return ENOENT; // Directory is invalid
    struct ext2_inode *p_inode = inode_by_index(parent->inode);
    struct ext2_dir_entry *found = NULL;
    int name_len = strlen(split->name);
    struct ext2_dir_entry *prev; // The entry prior to the one we are restoring
    int real_rec_len;
    int expected_rec;

    /* Find the file being restored */
    for (int b = 0; b < p_inode->i_blocks / (2 << sb->s_log_block_size); b++) {
        prev = (struct ext2_dir_entry *) (disk + BLOCK_SIZE * p_inode->i_block[b]);
        int s = prev->rec_len;
        while (s < 1024) {
            s += prev->rec_len;
            expected_rec = sizeof(struct ext2_dir_entry) + prev->name_len + 4 - (prev->name_len % 4);
            real_rec_len = prev->rec_len; // Real rec_len of the entry. Will be updated as deleted entries are parsed.
            while (expected_rec != real_rec_len) {
                struct ext2_dir_entry *tmp = (struct ext2_dir_entry *) (((char *) prev) + expected_rec);
                if (tmp->name_len == name_len && strncmp(split->name, tmp->name, tmp->name_len) == 0) { // Found entry to be restored
                    found = tmp;
                    break;
                }
                real_rec_len -= tmp->rec_len;
            }
            if (found != NULL) break;
            prev = (struct ext2_dir_entry *) (((char *) prev) + prev->rec_len);
        }
        if (found != NULL) break;
    }
    if (found == NULL) return ENOENT;

    /* Verify we have the data to restore the file */
    if (found->inode == 0) return ENOENT;
    if (inode_is_allocated(found->inode)) return ENOENT;
    struct ext2_inode *inode = inode_by_index(found->inode);
    for (int b = 0; b < inode->i_blocks / (2 << sb->s_log_block_size); b++) {
        if (block_is_allocated(inode->i_block[b])) return ENOENT;
    }
    /* Actually restore the file */
    realloc_inode(found->inode);
    inode->i_links_count = 1;
    inode->i_ctime = (unsigned) time(NULL);
    // Realloc blocks
    for (int b = 0; b < inode->i_blocks / (2 << sb->s_log_block_size); b++) {
        realloc_block(inode->i_block[b]);
    }
    // Add dir entry to linked list
    found->rec_len = prev->rec_len - expected_rec; // Update rec_len in case more files were modified in the dir since deletion
    prev->rec_len = expected_rec;
    return 0;
}
