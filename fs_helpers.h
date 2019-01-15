/*
 * Author(s): Changxiao Xie
 * COS 318, Fall 2018: Project 6 File System.
 * Implementation of a Unix-like file system.
*/
#ifndef FS_HELPERS
#define FS_HELPERS

// super block stuff
#define SUPER_BLOCK 0
#define MAGIC_NUM 0xabcd

typedef struct {
    uint32_t magic_num;
    uint32_t fs_size; // size of file system in blocks
    uint32_t inode_start; // first block where inodes are stored
    uint32_t max_num_inodes; // maximum number of inodes that can be in file system. scales with FS_SIZE
    uint32_t inode_count; // number of blocks allocated for inodes
    uint32_t ba_map_start; // first block where block allocation map is stored
    uint32_t ba_map_count; // number of blocks allocated for block allocation map
    uint32_t data_start; // first block where data blocks are stored
    uint32_t data_count; // number of blocks allocated for data
} super_block_t;

// inode stuff
#define TYPE_FREE 0
#define TYPE_DIRECTORY 1
#define TYPE_FILE 2
#define DATA_BLOCK_NUM 8 // each file can have 4MB of data

typedef struct {
    uint32_t size; // size of the file in bytes
    uint32_t fd_count; // number of open file descriptors for this file
    uint32_t links; // number of links to this file
    uint16_t in_use_blocks; // number of in use data blocks for this file
    uint16_t direct_blocks[DATA_BLOCK_NUM];
    uint16_t type; // type of the file (directory, free inode, file)
} inode_t; // total size of an inode is 32 bytes, so there are 16 inodes per block

// directory stuff
#define ROOT_DIRECTORY 0 // root directory is inode 0
#define MAX_FILE_NAME_COPY 32

typedef struct {
    uint16_t inode; // index of the file's inode
    char name[MAX_FILE_NAME_COPY]; // name of the file
} directory_entry_t; // there can be 15 directory entries per block plus 2 bytes padding, so 120 files per directory

// file stuff
#define MAX_FILE_DESCRIPTORS 256
typedef struct {
    bool_t open; // tracks if this entry is open
    uint16_t permissions; // the r/w permissions of this file
    uint16_t inode; // inode of file on disk
    uint16_t directory; // directory of file on disk
    uint32_t position; // current cursor position in bytes of file
} file_t;

// super block functions
void super_block_init(super_block_t *sblock, int fs_size);
super_block_t *super_block_read(char *block_buffer);
void super_block_write(char *block_buffer);

// data block functions
int get_free_data(super_block_t *super_block);
void data_free(int data_block, super_block_t *super_block);

// inode functions
void inode_init(inode_t *inode, int type);
int get_free_inode(super_block_t *super_block);
inode_t *inode_read(char *block_buffer, int inode_num, super_block_t *super_block);
void inode_write(char *block_buffer, int inode_num, super_block_t *super_block);
void inode_free(int inode_num, super_block_t *super_block);

// directory functions
int dir_add(char *name, int dir_inode_num, int file_inode_num, super_block_t *super_block);
int dir_remove(int dir_inode_num, char *name, super_block_t *super_block);
int dir_find(int dir_inode_num, char *name, super_block_t *super_block);

// file descriptor table functions
int fd_open(file_t *fd_table, int inode, int permissions, int directory);
void fd_close(file_t *fd_table, int fd_index);
int fd_dir_search(file_t *fd_table, int directory);

#endif

