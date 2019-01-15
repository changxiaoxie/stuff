#!/usr/bin/python

import os, sys, subprocess
fs_size_bytes = 1048576

def spawn_lnxsh():
    global p
    p = subprocess.Popen('./lnxsh', shell=True, stdin=subprocess.PIPE, stdout=subprocess.PIPE)

def issue(command):
    p.stdin.write(command + '\n')

def check_fs_size():
    fs_size = os.path.getsize('disk')
    if fs_size > fs_size_bytes:
        print "** File System is bigger than it should be (%s) **" %(pretty_size(fs_size))
    else:
        print "Size is fine"

def do_exit():
    issue('exit')
    return p.communicate()[0]

# Verify that mkfs zeroes out previous file system
def mkfs_test():
    print('*****Make File System Test*****')
    #create root directory with . and ..
    issue('mkfs')
    issue('ls')

    #make sure that fd_table works
    issue('open file1 3')
    issue('open file2 3')
    issue('open file3 3')
    issue('ls')

    #make sure that directories work
    issue('mkdir dir')
    issue('cd dir')
    issue('ls')
    issue('cd ..')
    issue('ls')

    #make sure that remaking file system zeros out everything
    issue('mkfs')
    issue('ls')
    issue('open file1 3')
    issue('ls')

    print do_exit()
    print('***********************')
    sys.stdout.flush()

def open_test():
    print('*****Open Files Test*****')
    issue('mkfs')
    #try to open read only, should fail
    issue('open file 1')

    #open with incorrect flags, should fail
    issue('open file 0')
    issue('open file 4')
    issue('open file -2')

    #open a file that does not exist
    issue('open file 3')

    #create a file and then open it
    issue('create file1 10')
    issue('open file1 3')

    #open a file write only, then read only
    issue('open file2 2')
    issue('open file2 1')

    #open a directory, read only should succeed but others should fail
    issue('open . 1')
    issue('open . 2')
    issue('open . 3')

    print do_exit()
    print('***********************')
    sys.stdout.flush()

def close_test():
    print('*****Close Files Test*****')
    issue('mkfs')
    #close a nonexistent fd, should fail
    issue('close 0')

    #close bad fd, should fail
    issue('close -1')
    issue('close file')
    issue('close 256')

    #open a file and then close it and then open it read only
    issue('open file 3')
    issue('close 0')
    issue('ls')
    issue('open file 1')

    #open a directory and then close it
    issue('open . 1')
    issue('close 0')

    #open multiple files and close them
    issue('open file1 3')
    issue('open file2 3')
    issue('open file3 3')
    issue('close 0')
    issue('close 2')
    issue('close 1')

    print do_exit()
    print('***********************')
    sys.stdout.flush()

def read_test():
    print('*****Read Files Test*****')
    issue('mkfs')

    #Reading from unopened file decriptor, should fail
    issue('read 0 1')

    #Reading from closed file descriptor, should fail
    issue('open file 3')
    issue('close 0')
    issue('read 0 1')

    #Reading from a directory
    issue('open . 1')
    issue('read 0 5')
    issue('close 0')

    #Reading from write only file, should fail
    issue('open file 2')
    issue('read 0 5')
    issue('close 0')

    #Read from read only and read-write only
    issue('open file 1')
    issue('open file1 3')
    issue('read 0 1')
    issue('read 1 1')
    issue('close 0')
    issue('close 1')

    #Read from two files at once
    issue('create a 10')
    issue('create b 10')
    issue('open a 1')
    issue('open b 1')
    issue('read 0 5')
    issue('read 1 5')
    issue('close 0')
    issue('close 1')

    #Read from one file from multiple file descriptors
    issue('open a 1')
    issue('open a 1')
    issue('read 0 5')
    issue('read 1 5')
    issue('close 0')
    issue('close 1')

    #Read negative bytes, should fail
    issue('open a 1')
    issue('read 0 -1')
    issue('close 0')

    #Read more than file length
    issue('open a 1')
    issue('read 0 15')
    issue('close 0')

    #Read from different points in a file
    issue('create c 40')
    issue('open c 1')
    issue('lseek 0 5')
    issue('read 0 5')
    issue('lseek 0 35')
    issue('read 0 5')
    issue('lseek 0 15')
    issue('read 0 10')
    issue('lseek 0 0')
    issue('read 0 20')

    print do_exit()
    print('***********************')
    sys.stdout.flush()

def write_test():
    print('*****Read Files Test*****')
    issue('mkfs')

    #write to unopened file descriptor, should fail
    issue('write 0 asdf')

    #write to closed file descriptor, should fail
    issue('open file 3')
    issue('close file 3')
    issue('write 0 asdf')

    #write to read only file, should fail
    issue('open file 1')
    issue('write 0 asdf')
    issue('close 0')

    #write to write and read-write only
    issue('open a 2')
    issue('open b 3')
    issue('write 0 qwer')
    issue('write 1 qwer')
    issue('close 0')
    issue('close 1')

    #write past end of file, should increase size to 10
    issue('open file 3')
    issue('stat file')
    issue('lseek 0 5')
    issue('write 0 world')
    issue('stat file')

    #write to different points in file
    issue('write 0 hello')
    issue('lseek 0 18')
    issue('write 0 asdf')
    issue('lseek 0 600')
    issue('write 0 world')
    issue('stat file')
    
    #write to very end of file
    issue('lseek 0 4095')
    issue('write 0 hahaha')
    issue('write 0 hahahaa') #should fail

    print do_exit()
    print('***********************')
    sys.stdout.flush()

def mkdir_test():
    print('*****Make Directory Test*****')
    issue('mkfs')

    #Try to make .. and ., should fail
    issue('mkdir .')
    issue('mkdir ..')

    #Make a new directory
    issue('mkdir dir')
    issue('ls')

    #Make a directory with same name, should fail
    issue('mkdir d')

    print do_exit()
    print('***********************')
    sys.stdout.flush()

def rmdir_test():
    print('*****Remove Directory Test*****')
    issue('mkfs')

    #Remove non existent directory, should fail
    issue('rmdir directory')

    #Try to remove . and .., should fail
    issue('rmdir .')
    issue('rmdir ..')

    #Remove an empty directory
    issue('mkdir directory')
    issue('ls')
    issue('rmdir directory')
    issue('ls')

    #Remove a full directory, should fail
    issue('mkdir directory')
    issue('cd directory')
    issue('create file 32')
    issue('cd ..')
    issue('rmdir directory')

    #Remove a directory with open file descriptors, should fail
    issue('cd directory')
    issue('open file 3')
    issue('unlink file')
    issue('cd ..')
    issue('rmdir directory')
    issue('close 0')
    issue('rmdir directory')

    print do_exit()
    print('***********************')
    sys.stdout.flush()

def link_test():
    print('*****Link Test*****')
    issue('mkfs')

    #link file to itself, should fail
    issue('create file 32')
    issue('link file file')

    #link . and .. to file, should fail
    issue('link . test')
    issue('link .. test')

    #link to non existent file, should fail
    issue('link test test1')

    #link existent files to each other, should fail
    issue('create file1 32')
    issue('link file1 file')

    #create links that are valid
    issue('link file file2')
    issue('link file file3')
    issue('link file file4')
    issue('ls')

    print do_exit()
    print('***********************')
    sys.stdout.flush()

def unlink_test():
    print('*****Unlink Test*****')
    issue('mkfs')

    #unlink nonexistent file, should fail
    issue('unlink file')

    #unlink . and .., should fail
    issue('unlink .')
    issue('unlink ..')

    #create a file and unlink
    issue('create file 32')
    issue('link file file1')
    issue('unlink file')
    issue('ls')

    #data of file is still available even after unlink
    issue('open file1 3')
    issue('unlink file1')
    issue('ls')
    issue('read 0 5')

    print do_exit()
    print('***********************')
    sys.stdout.flush()

def stat_test():
    print('*****Stat Test*****')
    issue('mkfs')

    #stat a non existent file, should fail
    issue('stat file')

    #stat . and ..
    issue('stat .')
    issue('stat ..')

    #create a file and stat
    issue('create file 32')
    issue('stat file')

    #write to a file and stat
    issue('open file 3')
    issue('write 0 asdfksadjfksl')
    issue('stat file')
    issue('close 0')

    #link and unlink
    issue('link file file2')
    issue('stat file')
    issue('stat file2')
    issue('unlink file')
    issue('stat file2')

    #stat a directory
    issue('mkdir directory')
    issue('stat directory')

    #stat a file with multiple blocks
    issue('create file 2500')
    issue('stat file')

    print do_exit()
    print('***********************')
    sys.stdout.flush()

def other_test():
    print('*****Other Tests*****')
    issue('mkfs')

    #cd something that doesn't exist, should fail
    issue('cd dir')
    
    #cd a file, should fail
    issue('create file 32')
    issue('cd file')

    #create file with max name and size
    issue('create asdfgasdfgasdfgasdfgasdfgasdfga 4096')
    issue('ls')

    #fill up a directory, last two should fail
    issue('mkfs')
    for i in range(0, 120):
        issue('create file' + str(i) + ' 120')
    issue('stat .')
    issue('ls')

    #remove first five entries of directory, should be swapped out with last 5
    for i in range(0, 5):
        issue('unlink file' + str(i))
    issue('ls')

    #remove 34 more entries from the directory, should be fine
    for i in range(5, 39):
        issue('unlink file' + str(i))
    issue('ls')
    issue('stat .')

    #read all characters from a big file
    issue('create big 4096')
    issue('open big 1')
    for i in range(0, 128):
        issue('read 0 32')

    print do_exit()
    print('***********************')
    sys.stdout.flush()

print "......Starting my tests\n\n"
sys.stdout.flush()
spawn_lnxsh()
mkfs_test()
spawn_lnxsh()
open_test()
spawn_lnxsh()
close_test()
spawn_lnxsh()
read_test()
spawn_lnxsh()
write_test()
spawn_lnxsh()
mkdir_test()
spawn_lnxsh()
rmdir_test()
spawn_lnxsh()
link_test()
spawn_lnxsh()
unlink_test()
spawn_lnxsh()
stat_test()
spawn_lnxsh()
other_test()

# Verify that file system hasn't grow too large
check_fs_size()
