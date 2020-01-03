#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"

unsigned char *disk;

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

    struct ext2_super_block *sb = (struct ext2_super_block *) (disk + 1024);
    printf("Inodes: %d\n", sb->s_inodes_count);
    printf("Blocks: %d\n", sb->s_blocks_count);
    struct ext2_group_desc *gd = (struct ext2_group_desc *) (disk + EXT2_BLOCK_SIZE + sizeof(struct ext2_super_block));
    printf("Block group:\n");
    printf("    block bitmap: %d\n", gd->bg_block_bitmap);
    printf("    inode bitmap: %d\n", gd->bg_inode_bitmap);
    printf("    inode table: %d\n", gd->bg_inode_table);
    printf("    free blocks: %d\n", sb->s_free_blocks_count);
    printf("    free inodes: %d\n", sb->s_free_inodes_count);
    printf("    used_dirs: %d\n", gd->bg_used_dirs_count);
    printf("Block bitmap: ");
    for (int byte = 0; byte < sb->s_blocks_count / 8; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            printf("%d", (disk[EXT2_BLOCK_SIZE * gd->bg_block_bitmap + byte] >> bit) & 1);
        }
        printf(" ");
    }
    printf("\nInode bitmap: ");
    for (int byte = 0; byte < sb->s_inodes_count / 8; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            printf("%d", (disk[EXT2_BLOCK_SIZE * gd->bg_inode_bitmap + byte] >> bit) & 1);
        }
        printf(" ");
    }
    printf("\n\nInodes:\n");
    for (int byte = 0; byte < sb->s_inodes_count / 8; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            int node_no = 8 * byte + bit + 1;             // Add 1 since nodes are indexed from 1
            if (node_no < EXT2_ROOT_INO || (node_no != EXT2_ROOT_INO && node_no <= EXT2_GOOD_OLD_FIRST_INO)) continue;             // Skip reserved inodes
            // Check if this inode is in use by the inode bitmap and print its information if it is
            if ((disk[EXT2_BLOCK_SIZE * gd->bg_inode_bitmap + byte] >> bit) & 1) {
                struct ext2_inode *inode = (struct ext2_inode*) (disk + (EXT2_BLOCK_SIZE * gd->bg_inode_table + sb->s_inode_size * (node_no - 1)));
                char mode;
                if (inode->i_mode & EXT2_S_IFDIR) mode = 'd';
                else if (inode->i_mode & EXT2_S_IFREG) mode = 'f';
                else mode = '0';
                printf("[%d] type: %c size: %d links: %d blocks: %d\n", node_no, mode, inode->i_size, inode->i_links_count, inode->i_blocks);
                printf("[%d] Blocks: ", node_no);
                for (int i = 0; i < inode->i_blocks / (2 << sb->s_blocks_count); i++) {
                    printf(" %d", inode->i_block[i]);
                }
                printf("\n");
            }
        }
    }
    printf("\nDirectory Blocks:\n");
    for (int byte = 0; byte < sb->s_inodes_count / 8; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            int node_no = 8 * byte + bit + 1;             // Add 1 since nodes are indexed from 1
            if (node_no < EXT2_ROOT_INO || (node_no != EXT2_ROOT_INO && node_no <= EXT2_GOOD_OLD_FIRST_INO)) continue;             // Skip reserved inodes
            // Check if this inode is in use by the inode bitmap and print its information if it is
            if ((disk[EXT2_BLOCK_SIZE * gd->bg_inode_bitmap + byte] >> bit) & 1) {
                struct ext2_inode *inode = (struct ext2_inode*) (disk + (EXT2_BLOCK_SIZE * gd->bg_inode_table + sb->s_inode_size * (node_no - 1)));
                if (!(inode->i_mode & EXT2_S_IFDIR)) continue;                 // Skip non-directory inodes
                for (int i = 0; i < inode->i_blocks / (2 << sb->s_blocks_count); i++) {
                    printf("   DIR BLOCK NUM: %d (for inode %d)", inode->i_block[i], node_no);
                    struct ext2_dir_entry *dir = (struct ext2_dir_entry*) (disk + EXT2_BLOCK_SIZE * inode->i_block[i]);
                    int rec_sum = 0; // Sum of rec_len printed already in this block, used to find when we are at the end of the block
                    char mode = '0';
                    while (rec_sum < 1024) {
                        switch (dir->file_type) {
                            case EXT2_FT_REG_FILE:
                                mode = 'f';
                                break;

                            case EXT2_FT_DIR:
                                mode = 'd';
                                break;
                        }
                        printf("\nInode: %d rec_len: %d name_len: %d type= %c name=%.*s", dir->inode, dir->rec_len, dir->name_len, mode, dir->name_len, dir->name);
                        rec_sum += dir->rec_len;
                        dir = (struct ext2_dir_entry*) (((char *) dir) + dir->rec_len);
                    }
                }
                printf("\n");
            }
        }
    }

    return 0;
}
