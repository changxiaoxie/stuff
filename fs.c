/*
 * Author(s): <Your name here>
 * COS 318, Fall 2015: Project 6 File System.
 * Implementation of a Unix-like file system.
*/
#include "util.h"
#include "common.h"
#include "block.h"
#include "fs.h"
#include "shellutil.h"
#include "fs_helpers.h"

#ifdef FAKE
#include <stdio.h>
#define ERROR_MSG(m) printf m;
#else
#define ERROR_MSG(m)
#endif

static super_block_t *super_block; // super block of file system
static char super_block_buffer[BLOCK_SIZE];
static int working_directory; // inode of current working directory
static file_t fd_table[MAX_FILE_DESCRIPTORS]; // file descriptor table

static int verify_open_fd(int fd) {
    if (fd >= MAX_FILE_DESCRIPTORS || fd < 0) { return -1; }
    if (fd_table[fd].open == FALSE) { return -1; }
    return 0;
}

static int verify_filename(char *fileName) {
    if (fileName == NULL) { return -1; }
    if (strlen(fileName) > MAX_FILE_NAME) { return -1; }
    return 0;
}

void fs_init( void) {
    block_init();
    super_block = super_block_read(super_block_buffer);
    if (super_block->magic_num == MAGIC_NUM) {
        working_directory = ROOT_DIRECTORY;
    }
    else {
        fs_mkfs();
    }
    // initialize file descriptor table
    bzero((char *)fd_table, sizeof(fd_table));
}

int fs_mkfs( void) {
    int i;
    char zero_block[BLOCK_SIZE];
    char block_buffer[BLOCK_SIZE];
    char super_block_buffer[BLOCK_SIZE];
    inode_t *root_dir;

    // zero all file system blocks
    bzero_block(zero_block);
    for (i = 0; i < FS_SIZE; i++) {
        block_write(i, zero_block);
    }

    // initialize and write the super block
    bzero_block(super_block_buffer);
    super_block_init(super_block, FS_SIZE);
    super_block_write(super_block_buffer);

    // create the root directory
    root_dir = inode_read(block_buffer, ROOT_DIRECTORY, super_block);
    inode_init(root_dir, TYPE_DIRECTORY);
    inode_write(block_buffer, ROOT_DIRECTORY, super_block);
    working_directory = ROOT_DIRECTORY;

    // add "." and ".." to root directory
    i = dir_add(".", ROOT_DIRECTORY, ROOT_DIRECTORY, super_block);
    if (i == -1) {
        block_write(0, zero_block);
        block_write(1, zero_block);
        return -1;
    }
    i = dir_add("..", ROOT_DIRECTORY, ROOT_DIRECTORY, super_block);
    if (i == -1) {
        block_write(0, zero_block);
        block_write(1, zero_block);
        return -1;
    }

    return 0;
}

int fs_open( char *fileName, int flags) {
    int file_inode_num;
    int status;
    int dirStatus;
    inode_t *file_inode;
    char block_buffer[BLOCK_SIZE];

    if (verify_filename(fileName) == -1) { return -1; }
    if (!(flags == FS_O_RDONLY || flags == FS_O_WRONLY || flags == FS_O_RDWR)) { return -1; }

    // check if file exists
    file_inode_num = dir_find(working_directory, fileName, super_block);

    // if it exists, check if it is a directory first then open it with permissions
    if (file_inode_num != -1) {
        file_inode = inode_read(block_buffer, file_inode_num, super_block);
        if (file_inode->type == TYPE_DIRECTORY && flags != FS_O_RDONLY) { return -1; }
        status = fd_open(fd_table, file_inode_num, flags);
        if (status == -1) { return -1; }
        file_inode->fd_count++;
        inode_write(block_buffer, file_inode_num, super_block);
    }
    // if it doesn't exist, check flags
    else {
        // if flags are read only, then we return -1
        if (flags == FS_O_RDONLY) { return -1; }
        // create the file
        file_inode_num = get_free_inode(super_block);
        if (file_inode_num == -1) { return -1; }

        file_inode = inode_read(block_buffer, file_inode_num, super_block);
        inode_init(file_inode, TYPE_FILE);
        status = fd_open(fd_table, file_inode_num, flags);
        if (status == -1) {
            inode_free(file_inode_num, super_block);
            return -1;
        }
        file_inode->fd_count++;
        inode_write(block_buffer, file_inode_num, super_block);

        // add file to current working directory
        dirStatus = dir_add(fileName, working_directory, file_inode_num, super_block);

        if (dirStatus == -1) {
            inode_free(file_inode_num, super_block);
            fd_close(fd_table, status);
            return -1;
        }
    }

    return status;
}

int fs_close( int fd) {
    int file_inode_num;
    inode_t *file_inode;
    char block_buffer[BLOCK_SIZE];

    if (verify_open_fd(fd) == -1) { return -1; }

    // decrement file descriptor count on inode
    file_inode_num = fd_table[fd].inode;
    file_inode = inode_read(block_buffer, file_inode_num, super_block);
    file_inode->fd_count--;
    inode_write(block_buffer, file_inode_num, super_block);

    // if file descriptor count is 0 and we have no links, we can free the inode
    if (file_inode->fd_count == 0 && file_inode->links == 0) {
        inode_free(file_inode_num, super_block);
    }

    // free the file descriptor table entry
    fd_close(fd_table, fd);

    return 0;
}

static int fs_read_helper(int position, inode_t *file_inode, char *buf, int count) {
    int data_block_index;
    int data_block_max;
    int data_block_num;
    int cursor;
    int bytes;
    int upcount;
    int downcount;
    char data_block_buffer[BLOCK_SIZE];
    char *block_pointer;

    upcount = 0; // how much we just read
    downcount = count; // how much left to read

    // get the first data block
    data_block_index = position / BLOCK_SIZE;
    data_block_max = 1 + (file_inode->size - 1) / BLOCK_SIZE;

    while (data_block_index < data_block_max && downcount > 0) {
        // set up data block
        data_block_num = file_inode->direct_blocks[data_block_index];
        block_read(data_block_num, data_block_buffer);

        block_pointer = data_block_buffer;
        cursor = position % BLOCK_SIZE;

        block_pointer += cursor;

        // if cursor is not 0 and we are on the last block with data
        if (cursor != 0 && data_block_index == data_block_max - 1) {
            // case where amount we want to read is less than the remaining file size
            if (downcount <= ((file_inode->size % BLOCK_SIZE) - cursor)) {
                bytes = downcount;
            }
            // case where amount we want to read is greater than remaining file size
            else {
                bytes = file_inode->size % BLOCK_SIZE - cursor;
            }
        }
        // if cursor is not 0 and we want to read less than the remainder of this block
        else if (cursor != 0 && downcount <= BLOCK_SIZE - cursor) {
            bytes = downcount;
        }
        // if cursor is not 0 and we want to read more than the remainder of this block
        else if (cursor != 0) {
            bytes = BLOCK_SIZE - cursor;
        }
        // if we are on the last block with data
        else if (data_block_index == data_block_max - 1) {
            // case where amount we want to read is less than remaining file size
            if (downcount <= (file_inode->size % BLOCK_SIZE)) {
                bytes = downcount;
            }
            // case where amount we want to read is greater than remaining file size
            else {
                bytes = file_inode->size % BLOCK_SIZE;
            }
        }
        // if we want to read less than this block
        else if (downcount <= BLOCK_SIZE) {
            bytes = downcount;
        }
        // if we want to read more than this block
        else {
            bytes = BLOCK_SIZE;
        }

        // copy from block to buffer
        bcopy((unsigned char *)block_pointer, (unsigned char *)buf, bytes);

        upcount += bytes;
        downcount -= bytes;
        position += bytes;
        data_block_index++;
        buf += bytes;
    }

    ASSERT(upcount + downcount == count);
    ASSERT(downcount >= 0);

    return upcount;
}

int fs_read( int fd, char *buf, int count) {
    int read_count;
    int file_inode_num;
    inode_t *file_inode;
    char inode_block_buffer[BLOCK_SIZE];

    if (verify_open_fd(fd) == -1) { return -1; }
    if (fd_table[fd].permissions == FS_O_WRONLY) { return -1; }
    if (buf == NULL) { return -1; }
    if (count < 0) { return -1; }

    // get the inode of the file
    file_inode_num = fd_table[fd].inode;
    file_inode = inode_read(inode_block_buffer, file_inode_num, super_block);

    // if position in fd is at the end or count is 0 we don't read anything
    if (count == 0) { return 0; }
    if (fd_table[fd].position >= file_inode->size) { return 0; }

    read_count = fs_read_helper(fd_table[fd].position, file_inode, buf, count);

    fd_table[fd].position += read_count;

    return read_count;
}

static int fs_write_helper(int position, inode_t *file_inode, char *buf, int count) {
    int upcount;
    int downcount;
    int data_block_index;
    int data_block_num;
    int cursor;
    int bytes;
    int new_block;
    int original_use_blocks;
    int i;
    char data_block_buffer[BLOCK_SIZE];
    char zero_block[BLOCK_SIZE];
    char *block_pointer;

    upcount = 0; // how much we already wrote
    downcount = count; // how much left to write
    bzero_block(zero_block);

    // get the first data block
    data_block_index = position / BLOCK_SIZE;

    // if the pointer is beyond the current end of file, write \0 in the intervening space
    if (position > file_inode->size) {
        // get the last used block
        block_read(file_inode->direct_blocks[file_inode->in_use_blocks - 1], data_block_buffer);
        // copy zeros from end of file to end of block
        bcopy((unsigned char *)zero_block, (unsigned char *)&data_block_buffer[file_inode->size % BLOCK_SIZE], BLOCK_SIZE - file_inode->size % BLOCK_SIZE);
        // write block to disk
        block_write(file_inode->direct_blocks[file_inode->in_use_blocks - 1], data_block_buffer);
    }
    
    // if we have a first data block that is greater than the current used blocks,
    // we need to allocate blocks until we get to the first data block
    if (data_block_index >= file_inode->in_use_blocks) {
        original_use_blocks = file_inode->in_use_blocks;
        for (; file_inode->in_use_blocks <= data_block_index; file_inode->in_use_blocks++) {
            new_block = get_free_data(super_block);
            // free all other data blocks and return to original state if we can't find new block
            if (new_block == -1) { 
                for (i = original_use_blocks; i < file_inode->in_use_blocks; i++) {
                    data_free(file_inode->direct_blocks[i], super_block);
                }
                file_inode->in_use_blocks = original_use_blocks;
                return -1; 
            }
            // write null characters to the new block
            block_write(new_block, zero_block);
            file_inode->direct_blocks[file_inode->in_use_blocks] = new_block;
        }
    }

    while (data_block_index < DATA_BLOCK_NUM && downcount > 0) {
        // if file doesn't have data block, we need to get one for it
        if (data_block_index == file_inode->in_use_blocks) {
            new_block = get_free_data(super_block);
            if (new_block == -1) { break; }
            file_inode->in_use_blocks++;
            file_inode->direct_blocks[data_block_index] = new_block;
        }

        // set up data block
        data_block_num = file_inode->direct_blocks[data_block_index];
        block_read(data_block_num, data_block_buffer);

        block_pointer = data_block_buffer;
        cursor = position % BLOCK_SIZE;

        block_pointer += cursor;

        // if cursor is not 0
        if (cursor != 0) {
            // if the amount we want to write is less than the end of the block
            if (downcount <= BLOCK_SIZE - cursor) {
                bytes = downcount;
            }
            else {
                bytes = BLOCK_SIZE - cursor;
            }
        }
        // if we want to write less than this block
        else if (downcount <= BLOCK_SIZE) {
            bytes = downcount;
        }
        // if we want to write more than this block
        else {
            bytes = BLOCK_SIZE;
        }

        // write from buffer to data block
        bcopy((unsigned char *)buf, (unsigned char *)block_pointer, bytes);
        // write data block to disk
        block_write(data_block_num, data_block_buffer);

        upcount += bytes;
        downcount -= bytes;
        position += bytes;
        data_block_index++;
        buf += bytes;
    }

    return upcount;
}
    
int fs_write( int fd, char *buf, int count) {
    int write_count;
    int file_inode_num;
    inode_t *file_inode;
    char inode_block_buffer[BLOCK_SIZE];

    if (verify_open_fd(fd) == -1) { return -1; }
    if (fd_table[fd].permissions == FS_O_RDONLY) { return -1; }
    if (buf == NULL) { return -1; }
    if (count < 0) { return -1; }

    // if count is greater than 0 but the file position is at the end (no bytes are written)
    if (count > 0 && fd_table[fd].position == BLOCK_SIZE * DATA_BLOCK_NUM) { return -1; }
    // if count is 0 return 0 and do nothing
    if (count == 0) { return 0; }

    // get the inode of the file
    file_inode_num = fd_table[fd].inode;
    file_inode = inode_read(inode_block_buffer, file_inode_num, super_block);

    write_count = fs_write_helper(fd_table[fd].position, file_inode, buf, count);

    if (write_count == -1) { return -1; }

    fd_table[fd].position += write_count;
    if (fd_table[fd].position > file_inode->size)
        file_inode->size = fd_table[fd].position;

    inode_write(inode_block_buffer, file_inode_num, super_block);

    return write_count;
}

int fs_lseek( int fd, int offset) {
    if (verify_open_fd(fd) == -1) { return -1; }
    if (offset < 0) { return -1; }
    fd_table[fd].position = offset;
    return offset;
}

int fs_mkdir( char *fileName) {
    int status;
    int inode_num;
    inode_t *inode;
    char block_buffer[BLOCK_SIZE];

    // check if fileName is valid or if directory name already exists
    if (verify_filename(fileName) == -1) { return -1; }
    status = dir_find(working_directory, fileName, super_block);
    if (status != -1) { return -1; }

    // allocate a new inode for this directory
    inode_num = get_free_inode(super_block);
    if (inode_num == -1) { return -1; }
    // initialize the inode into a directory
    inode = inode_read(block_buffer, inode_num, super_block);
    inode_init(inode, TYPE_DIRECTORY);
    inode_write(block_buffer, inode_num, super_block);

    // try to put this inode into current directory
    status = dir_add(fileName, working_directory, inode_num, super_block);
    if (status == -1) { 
        inode_free(inode_num, super_block);
        return -1; 
    }

    // try to put "." and ".." inodes into new directory
    status = dir_add(".", inode_num, inode_num, super_block);
    if (status == -1) {
        inode_free(inode_num, super_block);
        dir_remove(working_directory, fileName, super_block);
        return -1;
    }
    status = dir_add("..", inode_num, working_directory, super_block);
    if (status == -1) {
        inode_free(inode_num, super_block);
        dir_remove(working_directory, fileName, super_block);
        return -1;
    }

    return 0;
}

int fs_rmdir( char *fileName) {
    int inode_num;
    inode_t *inode;
    char block_buffer[BLOCK_SIZE];

    // can't remove these from the directory
    if (same_string(fileName, ".") || same_string(fileName, "..")) {
        return -1;
    }

    // get the directory from current directory
    if (verify_filename(fileName) == -1) { return -1; }
    inode_num = dir_find(working_directory, fileName, super_block);
    if (inode_num == -1) { return -1; }

    // check if the directory is valid and empty
    inode = inode_read(block_buffer, inode_num, super_block);
    if (inode->type != TYPE_DIRECTORY) { return -1; }
    if (inode->size != 2 * sizeof(directory_entry_t)) { return -1; }

    // remove the directory
    dir_remove(working_directory, fileName, super_block);

    // decrement its links
    inode->links--;
    if (inode->links == 0) {
        inode_free(inode_num, super_block);
    }
    else {
        inode_write(block_buffer, inode_num, super_block);
    }

    return 0;
}

int fs_cd( char *dirName) {
    int inode_num;
    inode_t *inode;
    char block_buffer[BLOCK_SIZE];

    if (verify_filename(dirName) == -1) { return -1; }
    inode_num = dir_find(working_directory, dirName, super_block);
    if (inode_num == -1) { return -1; }

    inode = inode_read(block_buffer, inode_num, super_block);
    if (inode->type != TYPE_DIRECTORY) { return -1; }

    working_directory = inode_num;

    return 0;
}

int fs_link( char *old_fileName, char *new_fileName) {
    int status;
    int inode_num;
    inode_t *file_inode;
    char block_buffer[BLOCK_SIZE];

    if (verify_filename(old_fileName) == -1 || verify_filename(new_fileName) == -1) { return -1; }
    if (dir_find(working_directory, new_fileName, super_block) != -1) { return -1; }

    // look for old_fileName in the directory. If not found return -1
    inode_num = dir_find(working_directory, old_fileName, super_block);
    if (inode_num == -1) { return -1; }

    file_inode = inode_read(block_buffer, inode_num, super_block);
    if (file_inode->type == TYPE_DIRECTORY) { return -1; }
    inode_write(block_buffer, inode_num, super_block);

    // if found, we make a new entry for the new_fileName
    status = dir_add(new_fileName, working_directory, inode_num, super_block);
    if (status == -1) { return -1; }

    // increment the link on file inode
    file_inode = inode_read(block_buffer, inode_num, super_block);
    file_inode->links++;
    inode_write(block_buffer, inode_num, super_block);
    
    return 0;
}

int fs_unlink( char *fileName) {
    int file_inode_num;
    inode_t *file_inode;
    char block_buffer[BLOCK_SIZE];

    if (verify_filename(fileName) == -1) { return -1; }
    // fine the inode of the fileName
    file_inode_num = dir_find(working_directory, fileName, super_block);
    // if not found we will return -1
    if (file_inode_num == -1) { return -1; }
    // if it is a directory return -1
    file_inode = inode_read(block_buffer, file_inode_num, super_block);
    if (file_inode->type == TYPE_DIRECTORY) { return -1; }

    // decrement its link
    file_inode->links--;
    inode_write(block_buffer, file_inode_num, super_block);

    // if links and fd_count are both 0 we can delete the inode
    if (file_inode->links == 0 && file_inode->fd_count == 0) {
        inode_free(file_inode_num, super_block);
    }

    // remove entry from directory
    dir_remove(working_directory, fileName, super_block);

    return 0;
}

int fs_stat( char *fileName, fileStat *buf) {
    int inode_num;
    inode_t *inode;
    char block_buffer[BLOCK_SIZE];

    if (fileName == NULL || buf == NULL) { return -1; }

    inode_num = dir_find(working_directory, fileName, super_block);
    if (inode_num == -1) { return -1; }
    inode = inode_read(block_buffer, inode_num, super_block);

    buf->inodeNo = inode_num;
    buf->type = (short) inode->type;
    buf->links = (char) inode->links;
    buf->size = (int) inode->size;
    buf->numBlocks = (int) inode->in_use_blocks;

    return 0;
}

static void print_one(inode_t *inode, char *name, int inode_num) {
    char spaces[] = "                                 ";
    writeStr(name);
    // print 32 - length(name) + 1 spaces
    spaces[MAX_FILE_NAME_COPY - strlen(name) + 1] = '\0';
    writeStr(spaces);
    
    if (inode->type == TYPE_DIRECTORY) {
        writeStr("D    ");
    }
    else {
        writeStr("F    ");
    }
    writeInt(inode_num);
    writeStr("     ");
    writeInt(inode->size);
    writeStr("\n");
}

void fs_ls( void) {
    inode_t *entry_inode;
    char inode_block_buffer[BLOCK_SIZE];
    inode_t *directory_inode;
    char dir_block_buffer[BLOCK_SIZE];
    char block_buffer[BLOCK_SIZE];
    int block_index;
    int block_max;
    int numEntries;
    int i;
    directory_entry_t *directory_entries;

    directory_inode = inode_read(dir_block_buffer, working_directory, super_block);
    block_max = directory_inode->in_use_blocks;
    
    writeStr("Name                             Type Inode Size\n");

    for (block_index = 0; block_index < block_max; block_index++) {
        // read a data block and get the directory entries
        block_read((int) (directory_inode->direct_blocks[block_index]), block_buffer);
        directory_entries = (directory_entry_t *)block_buffer;

        // see how many entries are in this data block
        if (block_index == block_max - 1) {
            numEntries = (directory_inode->size % BLOCK_SIZE) / sizeof(directory_entry_t);
        }
        else {
            numEntries = BLOCK_SIZE / sizeof(directory_entry_t);
        }
        // print the names of all the data blocks
        for (i = 0; i < numEntries; i++) {
            entry_inode = inode_read(inode_block_buffer, directory_entries[i].inode, super_block);
            print_one(entry_inode, directory_entries[i].name, directory_entries[i].inode);
        }
    }
}

