#include "ext2_utils.c"

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *gd;

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <absolute path on virtual disk>\n", argv[0]);
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    sb = (struct ext2_super_block *) (disk + EXT2_BLOCK_SIZE);
    gd = (struct ext2_group_desc *) (disk + EXT2_BLOCK_SIZE + sizeof(struct ext2_super_block));

    struct ext2_dir_entry *entry = get_dir_entry_by_path(argv[2], 0);
    if (entry == NULL) return ENOENT;
    if (entry->file_type == EXT2_FT_DIR) return EISDIR;
    // If this is the first entry in its block then make its inode 0
    if ((unsigned long) entry % 1024 == 0) {
        entry->inode = 0;
    } else { // Otherwise just update previous rec_len
        struct dir_name *name = split_path(argv[2]);
        struct ext2_dir_entry *next = get_dir_entry_by_path(name->parent, 1);
        struct ext2_dir_entry *c;
        while (next != entry) {
            c = next;
            next = (struct ext2_dir_entry *) (((char *) next) + next->rec_len);
        }

        c->rec_len += entry->rec_len;
    }
    struct ext2_inode *inode = inode_by_index(entry->inode);
    inode->i_links_count--;
    if (inode->i_links_count == 0) clear_entry(entry);
    return 0;
}
