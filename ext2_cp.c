#include <sys/stat.h>
#include "ext2_utils.c"

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *gd;

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <image file name> <path on local system> <absolute path on virtual disk>\n", argv[0]);
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

    struct dir_name *split_dir = split_path(argv[3]);
    char *parent = split_dir->parent;
    char *name;
    struct ext2_dir_entry *folder = get_dir_entry_by_path(argv[3], 1);
    struct ext2_dir_entry *new_entry;
    int inode_index;
    if (folder->file_type != EXT2_FT_DIR) return EEXIST;
    if (folder == NULL) return ENOENT;
    if (folder->file_type == EXT2_FT_DIR) { // folder is . entry in directory we will be copying to
        name = malloc(strlen(argv[2]));
        strcpy(name, argv[2]);
        name = strrchr(name, '/'); // Use local filename
        if (name == NULL) {
            name = argv[2]; // Handle copying from working directory
        } else { // Don't necessarily need all the memory alloc'd originally so get a more appropriate amount
            char *tmp = malloc(strlen(name) + 1);
            strcpy(tmp, name);
            free(name);
            name = tmp;
        }
        new_entry = add_new_entry(name, inode_by_index(folder->inode), 0);
    } else if (folder->file_type == EXT2_FT_REG_FILE) { // folder is the file we will be overwriting
        char *buffer = malloc(folder->name_len + 1);
        memcpy(buffer, folder->name, folder->name_len);
        memset(buffer + folder->name_len, '\0', 1);
        if (strcmp(buffer, split_dir->name) == 0) return EEXIST; // Returning from main so don't need to free buffer
        free(buffer);
        name = split_dir->name;
        new_entry = folder;
        clear_entry(folder);
        folder = get_dir_entry_by_path(parent, 1);
    } else {
        return EINVAL;  // invalid filetype
    }
    free(split_dir);
    inode_index = alloc_inode_index();
    if (inode_index == -1) return ENOSPC;
    // Open the file first to check validity
    FILE *file = fopen(argv[2], "r");
    if (file == NULL) return ENOENT;
    struct stat file_stat;
    stat(argv[2], &file_stat);
    int blocks_needed = file_stat.st_size / BLOCK_SIZE;
    if (blocks_needed == 0) blocks_needed = 1; // Still need a block for an empty file
    if (blocks_needed > 267 || blocks_needed > sb->s_free_blocks_count) return ENOSPC;

    struct ext2_inode *inode = inode_by_index(inode_index);
    inode_init(inode, EXT2_S_IFREG);
    new_entry->file_type = EXT2_FT_REG_FILE;
    new_entry->inode = inode_index;
    inode->i_size = file_stat.st_size;
    inode->i_links_count = 1;
    inode->i_blocks = inode->i_size / 512;
    // Write to data blocks
    int new_block;
    for (int b = 0; b < blocks_needed; b++) {
        new_block = alloc_data_block();
        if (new_block == -1) return ENOSPC;         // This shouldn't ever be true if the disk is consistent
        inode->i_block[b] = new_block;
        fread(disk + BLOCK_SIZE * new_block, 1, 1024, file);
    }
    return 0;
}
