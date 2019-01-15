/*
 * Author(s): Changxiao Xie
 * COS 318, Fall 2018: Project 6 File System.
 * Implementation of a Unix-like file system.
*/
#include "util.h"
#include "common.h"
#include "block.h"
#include "fs_helpers.h"

#ifdef FAKE
#include <stdio.h>
#define ERROR_MSG(m) printf m;
#else
#define ERROR_MSG(m)
#endif

// SUPER BLOCK STUFF

// initialize super block
void super_block_init(super_block_t *sblock, int fs_size) {
    ASSERT(fs_size >= 22); // this needs to be true otherwise max_num_inodes will be 0
    sblock->magic_num = MAGIC_NUM;
    sblock->fs_size = fs_size;
    sblock->inode_start = 1;
    sblock->max_num_inodes = (((uint32_t) (0.75 * fs_size)) / 16) * 16; // 75% of FS_SIZE and always a multiple of 16
    sblock->inode_count = 1 + (sblock->max_num_inodes - 1) / (BLOCK_SIZE / sizeof(inode_t));
    sblock->ba_map_start = sblock->inode_start + sblock->inode_count;
    sblock->ba_map_count = 1 + (fs_size - 1) / BLOCK_SIZE;
    sblock->data_start = sblock->ba_map_start + sblock->ba_map_count;
    sblock->data_count = fs_size - 1 - sblock->inode_count - sblock->ba_map_count;
}

// read in the super block from the file system
super_block_t *super_block_read(char *block_buffer) {
    block_read(SUPER_BLOCK, block_buffer);
    return (super_block_t *)block_buffer;
}

// write the super block to the file system
void super_block_write(char *block_buffer) {
    block_write(SUPER_BLOCK, block_buffer);
}

// DATA BLOCK STUFF

// look in the block allocation map to find a free data block
int get_free_data(super_block_t *super_block) {
    int i, j;
    char block_buffer[BLOCK_SIZE];

    for (i = super_block->ba_map_start; i < super_block->data_start; i++) {
        block_read(i, block_buffer);
        for (j = 0; j < BLOCK_SIZE; j++) {
            int index = (i - super_block->ba_map_start) * BLOCK_SIZE + j;
            if (index >= super_block->fs_size) { return -1; }
            if (index < super_block->data_start) { continue; }
            if (!block_buffer[j]) {
                block_buffer[j] = TRUE;
                block_write(i, block_buffer);
                return index;
            }
        }
    }

    return -1;
}

// free a data block by setting its block allocation map entry to FALSE
void data_free(int data_block, super_block_t *super_block) {
    char block_buffer[BLOCK_SIZE];
    if (data_block >= super_block->fs_size) { return; }
    if (data_block < super_block->data_start) { return; }
    // first zero out the data block
    bzero_block(block_buffer);
    block_write(data_block, block_buffer);
    // get the block allocation block and set corresponding entry to FALSE
    int ba_map_block = super_block->ba_map_start + (data_block / BLOCK_SIZE);
    block_read(ba_map_block, block_buffer);
    block_buffer[data_block % BLOCK_SIZE] = FALSE;
    block_write(ba_map_block, block_buffer);
}

// INODE STUFF

// initialize a new inode
void inode_init(inode_t *inode, int type) {
    inode->size = 0;
    inode->fd_count = 0;
    inode->links = 1;
    inode->in_use_blocks = 0;
    inode->type = type;
    bzero((char *)inode->direct_blocks, sizeof(inode->direct_blocks));
}

// get a free inode and return its index, otherwise return -1
int get_free_inode(super_block_t *super_block) {
    int block;
    int index;
    char inode_block_buffer[BLOCK_SIZE];
    inode_t *inodes;

    for (block = super_block->inode_start; block < super_block->ba_map_start; block++) {
        block_read(block, inode_block_buffer);
        inodes = (inode_t *)inode_block_buffer;
        for (index = 0; index < (BLOCK_SIZE / sizeof(inode_t)); index++) {
            if (inodes[index].type == TYPE_FREE)
                return (block - super_block->inode_start) * (BLOCK_SIZE / sizeof(inode_t)) + index;
        }
    }

    return -1;
}

// read from disk inode with index inode_num
inode_t *inode_read(char *block_buffer, int inode_num, super_block_t *super_block) {
    inode_t *inodes;
    int i;
    int block_num = inode_num / (BLOCK_SIZE / sizeof(inode_t)) + super_block->inode_start;

    if (inode_num >= super_block->max_num_inodes) { return NULL; }

    block_read(block_num, block_buffer);
    inodes = (inode_t *)block_buffer;
    i = inode_num % (BLOCK_SIZE / sizeof(inode_t));
    return &inodes[i];
}

// write to disk inode with index inode_num
void inode_write(char *block_buffer, int inode_num, super_block_t *super_block) {
    ASSERT(inode_num < super_block->max_num_inodes);
    int block_num = inode_num / (BLOCK_SIZE / sizeof(inode_t)) + super_block->inode_start;
    block_write(block_num, block_buffer);
}

// free inode with index inode_num
void inode_free(int inode_num, super_block_t *super_block) {
    int i;
    char inode_block_buffer[BLOCK_SIZE];
    inode_t *inode;

    if (inode_num >= super_block->max_num_inodes) { return; }

    inode = inode_read(inode_block_buffer, inode_num, super_block);
    inode->type = TYPE_FREE;
    for (i = 0; i < inode->in_use_blocks; i++) {
        data_free(inode->direct_blocks[i], super_block);
    }
    inode_write(inode_block_buffer, inode_num, super_block);
}

// DIRECTORY STUFF
static void str_copy(char *src, char *dest) {
    bcopy((unsigned char *)src, (unsigned char *)dest, strlen(src) + 1);
}

// read a directory entry
static directory_entry_t *directory_read(char *block_buffer, int data_block_num, int data_block_offset) {
    directory_entry_t *directory_entries;
    block_read(data_block_num, block_buffer);
    directory_entries = (directory_entry_t *)block_buffer;
    return &directory_entries[data_block_offset];
}

// write a directory entry
static void directory_write(char *block_buffer, int data_block_num) {
    block_write(data_block_num, block_buffer);
}

// add a file inode to a directory
int dir_add(char *name, int dir_inode_num, int file_inode_num, super_block_t *super_block) {
    inode_t *dir_inode;
    directory_entry_t *directory_entry;
    int current_num_entries;
    int max_num_entries;
    int data_block_index;
    int data_block_offset;
    int data_new_block;
    char inode_block_buffer[BLOCK_SIZE];
    char data_block_buffer[BLOCK_SIZE];

    dir_inode = inode_read(inode_block_buffer, dir_inode_num, super_block);
    current_num_entries = dir_inode->size / sizeof(directory_entry_t);
    max_num_entries = (BLOCK_SIZE / sizeof(directory_entry_t)) * DATA_BLOCK_NUM;

    // check if dir_inode is full and return -1 if full
    if (current_num_entries >= max_num_entries) {
        return -1;
    }

    // calculate the next available entry in the directory
    data_block_index = current_num_entries / (BLOCK_SIZE / sizeof(directory_entry_t));
    data_block_offset = current_num_entries % (BLOCK_SIZE / sizeof(directory_entry_t));

    // allocate a new data block if necessary
    if (data_block_offset == 0) {
        data_new_block = get_free_data(super_block);
        // if no free blocks return -1
        if (data_new_block == -1)
            return -1;
        dir_inode->in_use_blocks++;
        dir_inode->direct_blocks[data_block_index] = data_new_block;
        // add 2 to padding if it's not the first block
        if (data_block_index != 0)
            dir_inode->size += 2;
    }

    // add file to data block
    directory_entry = directory_read(data_block_buffer, dir_inode->direct_blocks[data_block_index], data_block_offset);
    directory_entry->inode = file_inode_num;
    str_copy(name, directory_entry->name);
    directory_write(data_block_buffer, dir_inode->direct_blocks[data_block_index]);

    dir_inode->size += sizeof(directory_entry_t);
    inode_write(inode_block_buffer, dir_inode_num, super_block);

    return 0;
}

// remove a file from a directory
int dir_remove(int dir_inode_num, char *name, super_block_t *super_block) {
    inode_t *dir_inode;
    directory_entry_t *directory_entry;
    directory_entry_t *last_entry;
    int current_num_entries;
    int data_block_index;
    int data_block_offset;
    int data_block_num;
    int max_index;
    int i;
    uint16_t last_block_index;
    int last_block_offset;
    char inode_block_buffer[BLOCK_SIZE];
    char data_block_buffer[BLOCK_SIZE];
    char last_block_buffer[BLOCK_SIZE];

    dir_inode = inode_read(inode_block_buffer, dir_inode_num, super_block);
    current_num_entries = dir_inode->size / sizeof(directory_entry_t);
    data_block_offset = current_num_entries % (BLOCK_SIZE / sizeof(directory_entry_t));

    // loop through data blocks to find the entry and remove it
    for (data_block_index = 0; data_block_index < dir_inode->in_use_blocks; data_block_index++) {
        data_block_num = dir_inode->direct_blocks[data_block_index];
        block_read(data_block_num, data_block_buffer);
        directory_entry = (directory_entry_t *)data_block_buffer;
        if (data_block_index == (dir_inode->in_use_blocks - 1) && data_block_offset != 0)
            max_index = data_block_offset;
        else
            max_index = BLOCK_SIZE / sizeof(directory_entry_t);
        for (i = 0; i < max_index; i++) {
            if (same_string(name, directory_entry[i].name)) {
                dir_inode->size -= sizeof(directory_entry_t);
                // if this is the only entry in directory, free the block, write back inode, and return
                if (dir_inode->size == 0) {
                    data_free(data_block_num, super_block);
                    inode_write(inode_block_buffer, dir_inode_num, super_block);
                    return 0;
                }
                // find last entry and copy into entry being replaced
                last_block_index = dir_inode->in_use_blocks - 1;
                if (data_block_offset == 0) {
                    last_block_offset = BLOCK_SIZE / sizeof(directory_entry_t) - 1;
                }
                else {
                    last_block_offset = data_block_offset - 1;
                }
                last_entry = directory_read(last_block_buffer, dir_inode->direct_blocks[last_block_index], last_block_offset);
                directory_entry[i].inode = last_entry->inode;
                str_copy(last_entry->name, directory_entry[i].name);

                // write back to block
                block_write(data_block_num, data_block_buffer);

                // free last block if necessary
                if (last_block_offset == 0) {
                    data_free(dir_inode->direct_blocks[last_block_index], super_block);
                    dir_inode->in_use_blocks--;
                    dir_inode->size -= 2;
                }

                inode_write(inode_block_buffer, dir_inode_num, super_block);

                return 0;
            }
        }
    }

    return -1;
}

// return the inode of the file we are looking for
int dir_find(int dir_inode_num, char *name, super_block_t *super_block) {
    inode_t *dir_inode;
    directory_entry_t *directory_entry;
    char inode_block_buffer[BLOCK_SIZE];
    char data_block_buffer[BLOCK_SIZE];
    int i, j;
    int current_num_entries;
    int data_block_offset;
    int max_index;

    dir_inode = inode_read(inode_block_buffer, dir_inode_num, super_block);
    current_num_entries = dir_inode->size / sizeof(directory_entry_t);
    data_block_offset = current_num_entries % (BLOCK_SIZE / sizeof(directory_entry_t));

    for (i = 0; i < dir_inode->in_use_blocks; i++) {
        block_read(dir_inode->direct_blocks[i], data_block_buffer);
        directory_entry = (directory_entry_t *)data_block_buffer;
        if (i == (dir_inode->in_use_blocks - 1) && data_block_offset != 0)
            max_index = data_block_offset;
        else
            max_index = BLOCK_SIZE / sizeof(directory_entry_t);
        for (j = 0; j < max_index; j++) {
            if (same_string(name, directory_entry[j].name)) {
                return directory_entry[j].inode;
            }
        }
    }

    return -1;
}

// FILE DESCRIPTOR STUFF

// open a file descriptor with the given inode and permissions
int fd_open(file_t *fd_table, int inode, int permissions, int directory) {
    int i;
    for (i = 0; i < MAX_FILE_DESCRIPTORS; i++) {
        if (fd_table[i].open == FALSE) {
            fd_table[i].open = TRUE;
            fd_table[i].permissions = permissions;
            fd_table[i].inode = inode;
            fd_table[i].directory = directory;
            fd_table[i].position = 0;
            return i;
        }
    }
    return -1;
}

// close a file descriptor with the given index
void fd_close(file_t *fd_table, int fd_index) {
    fd_table[fd_index].open = FALSE;
}

// search in fd_table for files with the specific directory
int fd_dir_search(file_t *fd_table, int directory) {
    int i;
    for (i = 0; i < MAX_FILE_DESCRIPTORS; i++) {
        if (fd_table[i].open == TRUE && fd_table[i].directory == directory) {
            return 0;
        }
    }
    return -1;
}