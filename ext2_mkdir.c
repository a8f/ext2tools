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
    
    sb = (struct ext2_super_block *)(disk + BLOCK_SIZE);
    gd = (struct ext2_group_desc *)(disk + BLOCK_SIZE + sizeof(struct ext2_super_block));

    char *full_path = argv[2];
    struct ext2_dir_entry *dir;
    // Check if directory exists
    dir = get_dir_entry_by_path(full_path, 0);
    if (dir != NULL) return EEXIST;
    if (strlen(full_path) == 1) {
        if (strcmp(full_path, "/") == 0) return EEXIST;
        return -ENOENT;
    }
    struct dir_name *split = split_path(full_path);
    char *parent = split->parent;
    char *name = split->name;
    free(split);
    dir = get_dir_entry_by_path(parent, 1);
    if (dir == NULL) {
        printf("Invalid dir\n");
        return ENOENT;
    }
    struct ext2_inode *inode = inode_by_index(dir->inode);
    struct ext2_dir_entry *new_dir = add_new_entry(name, inode, 0);
    new_dir->file_type = EXT2_FT_DIR;
    int new_inode_ind = alloc_inode_index();
    if (new_inode_ind == -1) return ENOSPC;
    int data_block = alloc_data_block();
    if (data_block == -1) return ENOSPC;
    struct ext2_inode *new_inode = inode_by_index(new_inode_ind);
    inode_init(new_inode, EXT2_S_IFDIR);
    new_inode->i_mode = EXT2_S_IFDIR;
    new_inode->i_links_count = 2; // link from parent and .
    new_inode->i_blocks = DISK_SECS_PER_BLOCK;
    new_inode->i_block[0] = data_block;
    new_inode->i_size = 1024;
    new_dir->inode = new_inode_ind;
    gd->bg_used_dirs_count++;
    // Make entry for .
    struct ext2_dir_entry *dot = add_new_entry(".", new_inode, 1);
    dot->inode = new_inode_ind;
    dot->file_type = EXT2_FT_DIR;
    // Make entry for ..
    struct ext2_dir_entry *dotdot = add_new_entry("..", new_inode, 0);
    dotdot->inode = dir->inode; // Set inode to parent's inode
    dotdot->file_type = EXT2_FT_DIR;
    inode_by_index(dir->inode)->i_links_count++; // dir->inode is valid or we wouldn't have gotten here
	return 0;
}
