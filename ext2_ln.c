#include "ext2_utils.c"

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *gd;

int main(int argc, char **argv) {
    int symlink;
    char *from_path;
    char *to_path;

    if (argc == 4) {
        symlink = 0;
        from_path = argv[2];
        to_path = argv[3];
    } else if (argc == 5 && strcmp(argv[2], "-s") == 0) {
        symlink = 1;
        from_path = argv[3];
        to_path = argv[4];
    } else {
        fprintf(stderr, "Usage: %s <image file name> (-s) <absolute path on virtual disk> <absolute path on virtual disk>\n", argv[0]);
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

    struct ext2_dir_entry *from_entry = get_dir_entry_by_path(from_path, 0);
    struct ext2_dir_entry *to_entry = get_dir_entry_by_path(to_path, 0);
    if (!symlink) {
        if (from_entry == NULL) return ENOENT; // Source doesn't exist
        if (from_entry->file_type == EXT2_FT_DIR) return EISDIR; // Trying to hardlink to a directory
    }
    if (to_entry != NULL) return EEXIST; // Destination exists
    struct ext2_dir_entry *new_entry;
    struct ext2_dir_entry *prev_entry; // Entry the new entry will be put after
    char *dest_name;
    // Set dest_name and prev_entry
    if (to_path[strlen(to_path) - 1] == '/') { // Use original filename if linking to a directory
        struct dir_name *from_dir = split_path(from_path);
        dest_name = from_dir->name;
        prev_entry = get_dir_entry_by_path(from_dir->parent, 1); // Already returned if source is invalid so a higher level directory is certainly valid
        prev_entry = last_entry(prev_entry);
        free(from_dir->parent);
        free(from_dir);
    } else {
        struct dir_name *to_dir = split_path(to_path);
        dest_name = to_dir->name;
        prev_entry = get_dir_entry_by_path(to_dir->parent, 1);
        if (prev_entry == NULL) return ENOENT; // Destination directory doesn't exist
        prev_entry = last_entry(prev_entry);
        free(to_dir->parent);
        free(to_dir);
    }

    int new_prev_rec = sizeof(struct ext2_dir_entry) + prev_entry->name_len + 4 - (prev_entry->name_len % 4);
    int needed_rec = sizeof(struct ext2_dir_entry) + strlen(dest_name) + 4 - (strlen(dest_name) % 4);
    // Handle no space for new entry
    if (prev_entry->rec_len - new_prev_rec < needed_rec) {
        struct ext2_inode *dir_inode = inode_by_index(prev_entry->inode);
        if (dir_inode->i_blocks > 267) return ENOSPC;
        int new_data_block = alloc_data_block();
        if (new_data_block == -1) return ENOSPC;
        dir_inode->i_block[dir_inode->i_blocks / 2 + 1] = new_data_block;
        dir_inode->i_blocks++;
        new_entry = (struct ext2_dir_entry *) (disk + BLOCK_SIZE * new_data_block);
        new_entry->rec_len = 1024;
    } else { // Just add after last entry if there's space
        new_entry = (struct ext2_dir_entry *) (((char *) prev_entry) + new_prev_rec);
        new_entry->rec_len = prev_entry->rec_len - new_prev_rec;
        prev_entry->rec_len = new_prev_rec;
    }

    new_entry->name_len = strlen(dest_name);
    memcpy(new_entry->name, dest_name, new_entry->name_len);

    if (symlink) { // Make symlink
        new_entry->file_type = EXT2_FT_SYMLINK;
        int new_inode_ind = alloc_inode_index();
        if (new_inode_ind == -1) return ENOSPC;
        int new_block = alloc_data_block();
        if (new_block == -1) return ENOSPC;
        struct ext2_inode *new_inode = inode_by_index(new_inode_ind);
        inode_init(new_inode, EXT2_S_IFLNK);
        new_inode->i_size = strlen(from_path);
        new_inode->i_links_count = 1;
        new_inode->i_blocks = DISK_SECS_PER_BLOCK;
        new_inode->i_block[0] = new_block;
        memcpy(disk + BLOCK_SIZE * new_block, from_path, new_inode->i_size);
        new_entry->inode = new_inode_ind;
    } else { // Make hardlink
        new_entry->file_type = EXT2_FT_REG_FILE;
        new_entry->inode = from_entry->inode;
        struct ext2_inode *from_inode = inode_by_index(from_entry->inode);
        from_inode->i_links_count++;
    }

    return 0;
}
