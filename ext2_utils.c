#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include "ext2.h"

#define BLOCK_SIZE EXT2_BLOCK_SIZE
#define DISK_SECS_PER_BLOCK 2

extern unsigned char* disk;
extern struct ext2_super_block *sb;
extern struct ext2_group_desc *gd;

struct ext2_inode *inode_by_index(int index);
struct ext2_dir_entry *last_entry(struct ext2_dir_entry *dir);
int alloc_data_block();
int inode_is_allocated(int inode_index);
int block_is_allocated(int block);
void realloc_inode(int inode_index);
void realloc_block(int block);

// Directory name entry
struct dir_name {
    char *parent; // Name of parent folder of this entry
    char *name; // Name of this entry
    int trailing_slash; // 0 iff the last character in the entry's path was '/'
};

// Returns 0 iff the inode is 0 in the inode bitmap
int inode_is_allocated(int inode_index) {
    int byte_off = BLOCK_SIZE * gd->bg_inode_bitmap + (inode_index-1) / 8;
    int bit = (inode_index - 1) % 8;
    return ((disk[byte_off] >> bit) && 1);
}

// Returns 0 iff the block is 0 in the block bitmap
int block_is_allocated(int block) {
        int o = BLOCK_SIZE * gd->bg_block_bitmap + (block - 1) / 8;
        int b = (block - 1) % 8;
        return ((disk[o] >> b) & 1);
}

/*
 * Allocates a specific inode in the inode bitmap
 */
void realloc_inode(int inode_index) {
    if (inode_index == 0) return;
    int offset = BLOCK_SIZE * gd->bg_inode_bitmap + (inode_index - 1) / 8;
    int bit = (inode_index - 1) / 8;
    disk[offset] |= 256 >> (8 - bit);
    sb->s_free_inodes_count--;
    gd->bg_free_inodes_count--;
}


/*
 * Allocates a specific block in the block bitmap
 */
void realloc_block(int block) {
    if (block == 0) return;
    int offset = BLOCK_SIZE * gd->bg_block_bitmap + (block - 1) /8;
    int bit = (block - 1) % 8;
    disk[offset] |= 256 >> (8-bit);
    sb->s_free_blocks_count--;
    gd->bg_free_blocks_count--;
}

// Zeroes the block bitmap entry for inode (1-indexed)
void zero_inode_bitmap(int inode) {
    int bit = (inode - 1) % 8;
    int byte_off = BLOCK_SIZE * gd->bg_inode_bitmap + (inode-1)/8;
    switch (8 - bit) {
        case 1:
            disk[byte_off] &= 127;
            break;
        case 2:
            disk[byte_off] &= 191;
            break;
        case 3:
            disk[byte_off] &= 223;
            break;
        case 4:
            disk[byte_off] &= 239;
            break;
        case 5:
            disk[byte_off] &= 247;
            break;
        case 6:
            disk[byte_off] &= 251;
            break;
        case 7:
            disk[byte_off] &= 253;
            break;
        case 8:
            disk[byte_off] &= 254;
    }
}
// Zeroes the block bitmap entry for block (1-indexed)
void zero_block_bitmap(int block) {
    int bit = (block - 1) %8;
    int byte_off = BLOCK_SIZE * gd->bg_block_bitmap + (block-1)/8;
    switch (8 - bit) {
        case 1:
            disk[byte_off] &= 127;
            break;
        case 2:
            disk[byte_off] &= 191;
            break;
        case 3:
            disk[byte_off] &= 223;
            break;
        case 4:
            disk[byte_off] &= 239;
            break;
        case 5:
            disk[byte_off] &= 247;
            break;
        case 6:
            disk[byte_off] &= 251;
            break;
        case 7:
            disk[byte_off] &= 253;
            break;
        case 8:
            disk[byte_off] &= 254;
    }
}
/*
 * Zeroes the block bitmap entries for inode->block
 */
void clear_inode_blocks(struct ext2_inode *inode) {
    for (int b = 0; b < inode->i_blocks / (2 << sb->s_log_block_size); b++) {
        zero_block_bitmap(inode->i_block[b]);
        sb->s_free_blocks_count++;
        gd->bg_free_blocks_count++;
    }
}

/*
 * Zeroes the block and inode bitmaps for dir->inode->block and dir->inode
 * Doesn't actually remove the entry
 */
void clear_entry(struct ext2_dir_entry *dir) {
    struct ext2_inode *inode = inode_by_index(dir->inode);
    clear_inode_blocks(inode);
    inode->i_dtime = (unsigned)time(NULL);
    zero_inode_bitmap(dir->inode);
    sb->s_free_inodes_count++;
    gd->bg_free_inodes_count++;
}
/*
 * Adds a new entry to the directory pointed to by inode
 * Precondition is_first_entry != 0 iff the new entry will be the first entry in the block
 * Returns pointer to new entry or NULL if entry couldn't be created
 *** SET inode AND file_type AFTER CALLING ***
 */
struct ext2_dir_entry *add_new_entry(char *entry_name, struct ext2_inode *inode, int is_first_entry) {
    struct ext2_dir_entry *new_dir = NULL;
    int name_len = strlen(entry_name);
    int last_new_rec;
    int i_block_max_ind = inode->i_blocks / DISK_SECS_PER_BLOCK - 1;
    struct ext2_dir_entry *last;
    last = (struct ext2_dir_entry *)(disk + BLOCK_SIZE * inode->i_block[i_block_max_ind]);
    if (is_first_entry) {
        new_dir = last;
        new_dir->rec_len = 1024;
    }
    else {
        last = last_entry(last);
        last_new_rec = sizeof(struct ext2_dir_entry) + last->name_len + 4 - (last->name_len % 4);
        // If there's no space for the new entry then get another data block and add the entry to it
        if (last->rec_len - last_new_rec < sizeof(struct ext2_dir_entry) + name_len + 4 - (name_len % 4)) {
            int new_block = alloc_data_block();
            if (new_block == -1) return NULL;
            inode->i_blocks += DISK_SECS_PER_BLOCK;
            inode->i_block[i_block_max_ind + 1] = new_block;
            new_dir = (struct ext2_dir_entry *)(disk + BLOCK_SIZE * new_block);
            new_dir->rec_len = 1024;
        }
        else {
            new_dir = (struct ext2_dir_entry *)(((char *)last) + last_new_rec);
            new_dir->rec_len = last->rec_len - last_new_rec; // This %4 = 0 since rec_len and last_new_rec %4 = 0
            last->rec_len = last_new_rec;
        }
    }
    new_dir->name_len = name_len;
    memcpy(new_dir->name, entry_name, name_len); // Copy name without null terminator
    return new_dir;
}

/*
 * Returns last entry in block dir is in
 * Precondition: dir is the first entry in its block
 */
struct ext2_dir_entry *last_entry(struct ext2_dir_entry *dir) {
    struct ext2_dir_entry *r = dir;
    int sum = dir->rec_len;
    while (sum % BLOCK_SIZE != 0) {
        r = (struct ext2_dir_entry *)(((char *)r) + r->rec_len);
        sum += r->rec_len;
    }
    return r;
}

/*
 * Returns dir_name* with dir_name->parent = the directory name and
 * dir_name->name = the most nested folder's name or NULL if path is /
 * Returns NULL if path is invalid
 *** Caller is responsible for freeing the returned struct and its members ***
 */
struct dir_name *split_path(char *full_path) {
    char *path = malloc(strlen(full_path) + 1);
    strcpy(path, full_path);
    int extra_chars = 0;
    int path_len = strlen(path);
    struct dir_name *ret = malloc(sizeof(struct dir_name));
    ret->trailing_slash = 0;
    if (path_len == 1) {
        if (path[0] != '/') {
            free(ret);
            free(path);
            return NULL;
        }
        char *parent = malloc(2);
        strcpy(parent, path);
        ret->parent = parent;
        ret->name = NULL;
        free(path);
        return ret;
    }
    char *name = strrchr(path, '/');
    if (name == NULL) {
        free(path);
        return NULL; // If there are no / the path is invalid
    }
        
    // Handle trailing /
    if (strcmp(name, "/") == 0) {
        extra_chars++;
        char *buffer = malloc(path_len);
        memcpy(buffer, path, path_len);
        buffer[path_len - 1] = '\0';
        char *tmp = strrchr(buffer, '/');
        // Buffer was potentially allocated much more memory than required for the filename so copy from buffer into name and free buffer
        name = malloc(strlen(tmp) + 1);
        strcpy(name, tmp);
        free(buffer);
        ret->trailing_slash = 1;
    }
    int name_len = strlen(name) + extra_chars; // Calculate name length before trimming folder name
    // Remove preceding / maintaining name pointer validity
    if (name[0] == '/') {
        memmove(name, name + 1, strlen(name));
    }
    char *parent = malloc(path_len - name_len + 1);
    memcpy(parent, path, path_len - name_len);
    memcpy(parent + path_len - name_len, "\0", 1);
    ret->parent = parent;
    ret->name = malloc(strlen(name) + 1);
    strcpy(ret->name, name);
    free(path);
    return ret;
}
/*
 * Initialize inode with i_mode of mode
 * i_links_count is set to 0
 */
void inode_init(struct ext2_inode *inode, unsigned short mode) {
    inode->i_mode = mode;
    inode->i_ctime = (unsigned)time(NULL);
}

/*
 * Returns pointer to the inode with index index or NULL if index is invalid 
 */
struct ext2_inode* inode_by_index(int index) {
	if (index < 1 || index > sb->s_inodes_count) return NULL;
	return (struct ext2_inode *)(disk + (BLOCK_SIZE * gd->bg_inode_table + sb->s_inode_size * (index - 1)));
}

/*
 * Returns index of allocated inode or -1 if there are no inodes left
 */ 
int alloc_inode_index() {
    if (sb->s_free_inodes_count == 0) return -1;
    for (int byte = 0; byte < sb->s_inodes_count / 8; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            int node_no = bit + 8*byte + 1;
            if (node_no <= EXT2_GOOD_OLD_FIRST_INO) continue; // Skip reserved inodes
            int byte_off = BLOCK_SIZE * gd->bg_inode_bitmap + byte;
            if (!((disk[byte_off] >> bit) & 1)) {
				disk[byte_off] |= 256 >> (8 - bit);
                sb->s_free_inodes_count--;
                gd->bg_free_inodes_count--;
                return node_no;
            }
        }
    }
    return -1;
}

/*
 * Returns pointer to allocated inode or NULL if there are no inodes left
 */
struct ext2_inode* alloc_inode() {
	int index = alloc_inode_index();
	return inode_by_index(index);
}

/*
 * Returns the index of the allocated data block or -1 if there are no free blocks
 */
int alloc_data_block() {
    if (sb->s_free_blocks_count == 0) return -1;
    unsigned int byte_off;
    for (int byte = 0; byte < sb->s_blocks_count / 8; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            byte_off = BLOCK_SIZE * gd->bg_block_bitmap + byte;
            if (((disk[byte_off] >> bit) & 1) == 0) { // If block isn't allocated
                disk[byte_off] |= 256 >> (8-bit); // Mark block allocated in bitmap
                sb->s_free_blocks_count--;
                gd->bg_free_blocks_count--;
                return byte*8 + bit + 1;
            }   
        }
    }
    return -1;
}

/*
 * Returns entry for the last part of the path, or entry for the first entry in the directory if enter_final_dir != 0 and the last part of the path is a folder, or NULL if path doesn't exist
 * Precondition path is a syntactically valid path
 */
struct ext2_dir_entry* get_dir_entry_by_path(char *path, int enter_final_dir) {
    struct ext2_inode *inode = (struct ext2_inode *)(disk + BLOCK_SIZE * gd->bg_inode_table + sb->s_inode_size * (EXT2_ROOT_INO - 1)); 
    if (strlen(path) == 1) {
        if (strcmp(path, "/") == 0) {
            return (struct ext2_dir_entry *)(disk + BLOCK_SIZE * inode->i_block[0]);
        }
        else return NULL;
    }
    char *copy = malloc(strlen(path) + 1);
    strcpy(copy, path);
    char *str = strtok(copy, "/");
    int found = 0;
    struct ext2_dir_entry *dir = (struct ext2_dir_entry *)(disk + BLOCK_SIZE * inode->i_block[0]);
    char dir_buf[EXT2_NAME_LEN + 1];
    int sum;
    while (str != NULL) {
        char *next_str = strtok(NULL, "/");
        found = 0;
        for (int i = 0; i < inode->i_blocks / (2 << sb->s_log_block_size); i++) {
            if (inode->i_block[i] == 0) break; // sanity check
            dir = (struct ext2_dir_entry *)(disk + BLOCK_SIZE * inode->i_block[i]);
            sum = 0;
            //if (next_str != NULL && dir->file_type != EXT2_FT_DIR) continue;
            while (sum < BLOCK_SIZE) {
                memcpy(dir_buf, dir->name, dir->name_len);
                memcpy(dir_buf + dir->name_len, "\0", 1);
                if (strncmp(str, dir_buf, strlen(str)) == 0) {
                    inode = (struct ext2_inode *)(disk + BLOCK_SIZE * gd->bg_inode_table + sb->s_inode_size * (dir->inode-1));
                    found = 1;
                    break;
                }
                sum += dir->rec_len;
                dir = (struct ext2_dir_entry *)(((char *)dir) + dir->rec_len);
            }
            if (found) break;
        }
        if (!found){ //&& next_str != NULL) {
            free(copy);
            return NULL;
        }
        str = next_str;
    }
    // Get first entry in the directory if final entry in path is a directory
    if (enter_final_dir && dir->file_type == EXT2_FT_DIR) dir = (struct ext2_dir_entry *)(disk + BLOCK_SIZE * inode->i_block[0]);
    free(copy);
    return dir;
}
