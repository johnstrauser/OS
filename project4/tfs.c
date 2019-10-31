/*
 *  Copyright (C) 2019 CS416 Spring 2019
 *	
 *	Tiny File System
 *
 *	File:	tfs.c
 *  Author: Yujie REN
 *	Date:	April 2019
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "tfs.h"

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here

struct superblock * sb;

/*
 * Get available inode block number from bitmap
 */
int get_avail_ino() {
	// Step 1: Read inode bitmap from disk
	unsigned char buffer [BLOCK_SIZE];
	bio_read(sb->i_bitmap_blk, buffer);
	
	// Step 2: Traverse inode bitmap to find an available slot
	int curr_ino = 0;
	while(curr_ino < sb->max_inum && get_bitmap((bitmap_t)buffer, curr_ino) == 1) {
		curr_ino++;
	}
	
	if (curr_ino >= sb->max_inum) {
		return -1;
	}
	
	// Step 3: Update inode bitmap and write to disk
	set_bitmap((bitmap_t)buffer, curr_ino);
	bio_write(sb->i_bitmap_blk, buffer);
	//printf("[get avail ino] block num: %d\n", sb->i_start_blk + curr_ino);
	return curr_ino;
}

/*
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
	// Step 1: Read data block bitmap from disk
	unsigned char buffer [BLOCK_SIZE];
	bio_read(sb->d_bitmap_blk, buffer);
	
	// Step 2: Traverse data block bitmap to find an available slot
	int curr_blkno = 0;
	while(curr_blkno < sb->max_dnum && get_bitmap((bitmap_t)buffer, curr_blkno) == 1) {
		curr_blkno++;
	}
	
	if (curr_blkno >= sb->max_dnum) {
		return -1;
	}
	
	// Step 3: Update data block bitmap and write to disk
	set_bitmap((bitmap_t)buffer, curr_blkno);
	bio_write(sb->d_bitmap_blk, buffer);
	//printf("[get avail blkno] block num: %d\n", sb->d_start_blk + curr_blkno);
	return curr_blkno;
}

int getInodeBlockNum(uint16_t ino) {
	return (unsigned int) ino / (BLOCK_SIZE / sizeof(struct inode));
}

int getInodeBlockIndex(uint16_t ino) {
	return ((unsigned int) ino % (BLOCK_SIZE / sizeof(struct inode))) * sizeof(struct inode);
}

/* 
 * inode operations
 */

int readi(uint16_t ino, struct inode *inod) {
	if (ino > MAX_INUM){
		printf("ERROR [readi] input ino is greater than the MAX_INUM\n");
		return -1;
	}
 	// Step 1: Get the inode's on-disk block number
 	unsigned int inode_blkno = getInodeBlockNum(ino);
 	
 	// Step 2: Get offset of the inode in the inode on-disk block
 	unsigned int inode_offset = getInodeBlockIndex(ino);
 	
 	// Step 3: Read the block from disk and then copy into inode structure
 	char buf[BLOCK_SIZE];
 	bio_read(sb->i_start_blk + inode_blkno, buf);
 	struct inode * temp_inode = malloc(sizeof(struct inode));
 	memcpy(temp_inode, (buf + inode_offset), sizeof(struct inode));
 	*inod = *temp_inode;
 	return 0;
 }

int writei(uint16_t ino, struct inode * inod) {
	if (ino > MAX_INUM){
		printf("ERROR [writei] input ino is greater than the MAX_INUM\n");
		return -1;
	}
	// Step 1: Get the inode's on-disk block number
 	unsigned int inode_blkno = getInodeBlockNum(ino);
 	
 	// Step 2: Get offset of the inode in the inode on-disk block
 	unsigned int inode_offset = getInodeBlockIndex(ino);
	
	// Step 3: Write inode to disk
	char buf[BLOCK_SIZE];
	bio_read(sb->i_start_blk + inode_blkno, buf);
	memcpy(buf + inode_offset, inod, sizeof(struct inode));
	bio_write(sb->i_start_blk + inode_blkno, buf);
	return 0;
}

/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirr) {
	//printf("[dir_find] STARTING dir_find, ino of the directory we are LOOKING INSIDE: %d, FNAME we're LOOKING FOR: %s\n", ino, fname);
	
	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	struct inode * currentDirInode = malloc(sizeof (struct inode));
	int readiresult = readi(ino, currentDirInode);
  
	if (readiresult != 0) {
		//printf("[dir_find] readi failed to find the inode of the dir we're looking in: %s\n", fname);
		return -ENOENT;
	}
	
	if (currentDirInode->type != DIRTYPE) {
		// the inode were searching in is not a directory
		return -1;
	}

	// Step 2: Get data block of current directory from inode
	int i;
	for (i = 0; i < 16; i++) {
		int data_block = currentDirInode->direct_ptr[i];
		if (data_block < sb->d_start_blk || (data_block > (sb->d_start_blk + sb->max_dnum))) {
			// Out of valid data blocks to look through
			//printf("[dir_find] INVALID BLOCK NUM: %d, for search through DIR: %d for fname: %s\n", data_block, ino, fname);
			continue;
		}
		//printf("[dir_find] Data block num: %d\n", data_block);

		//printf("[dir_find] i: %d, data block: %d\n", data_block);
		void * buffer = malloc(BLOCK_SIZE);
		bio_read(data_block, buffer);
		int j;		
		for (j = 0; j < 16; j++) {
			// Step 3: Read directory's data block and check each directory entry.
		    struct dirent * d = malloc(sizeof(struct dirent));
			memcpy(d, buffer + (j * sizeof(struct dirent)), sizeof(struct dirent));
			//printf("[dir_find] d-> %s, d->valid: %d,  fname: %s\n", d->name, d->valid, fname);
			if (strcmp(d->name, fname) == 0 && d->valid == 1) {
				//printf("[dir_find] FOUND a dirent for fname: %s, inside ino: %d\n", fname, ino);
				memcpy(dirr, d, sizeof(struct dirent));
				//free(d);
				//free(currentDirInode);
				//free(buffer);
				return 0;		
			}
			//free(d);
		}
		//free(buffer);
	}
	//free(currentDirInode);
  	//printf("[dir_find] COULDN'T FIND a dirent for fname: %s, inside ino: %d\n", fname, ino);
	return -1;
}	

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {
	//printf("[dir_add] STARTING dir_add with ino: %d and fname: %s\n", f_ino, fname);
	
	// Step 1: Look inside this directory to see if the dirent we're trying to add already exists
	struct dirent * direntToAdd = malloc(sizeof(struct dirent));
	if(dir_find(dir_inode.ino, fname, name_len, direntToAdd) != -1) {
		printf("ERROR [dir_add] found a duplicate dirent inside: %d, for fname: %s\n", dir_inode.ino, fname);
		return -1;
	}

	// Step 2: Find an empty block inside dir_inode's direct pointers to add this new dirent
	// If we come accross a direct pointer that is not in the range of valid data block offsets:
		// Then check the data block bitmap for any free blocks
		// If the data bitmap is all full, return -1 -> no space in this directory
	// Once an empty data block and offset are found, insert a new dirent and update the metadata of this parent directory
	int found = -1;
	int i;  
  	for (i = 0; i < 16; i++) {
		if (found == 1) {
			break;
		}
		int dataBlockNum = dir_inode.direct_ptr[i];
		// Check if this data block num is valid
		if (dataBlockNum < sb->d_start_blk || (dataBlockNum > (sb->d_start_blk + sb->max_dnum))) {
			int blk_no = get_avail_blkno();
			if (blk_no == -1) {
				// No more available data blocks
				printf("ERROR [dir_add] no more available data blocks to add this new dirent\n");
				return -1;
			} else {
				// Allocate this new data block
				dataBlockNum = sb->d_start_blk + blk_no;
				dir_inode.direct_ptr[i] = dataBlockNum;
				void * buffer = malloc(4096);
				bio_read(sb->d_bitmap_blk, buffer);
				set_bitmap((bitmap_t)buffer, dataBlockNum);	
			}
		}
		// Load this data block into buffer
		void * buffer = malloc(BLOCK_SIZE);
		bio_read(dataBlockNum, buffer);
		
		// Search through the block to find a spot to insert a new dirent
		int j;
		for (j = 0; j < 16; j++) {
			struct dirent * temp = malloc(sizeof(struct dirent));
			int dataBlockOffset = j * sizeof(struct dirent);
			memcpy(temp, buffer + dataBlockOffset, sizeof(struct dirent));
			if (temp->valid != 1) {
				// Found a free offset to insert a new dirent
				found = 1;
				
				// Create a new dirent to insert
				struct dirent * toInsert = malloc(sizeof(struct dirent));
				toInsert->ino = f_ino;
				toInsert->valid = 1;
				strcpy(toInsert->name, fname);
				
				// Write the new dirent to the offset we found
				void * b = malloc(BLOCK_SIZE);
				bio_read(dataBlockNum, b);
				memcpy(b + dataBlockOffset, toInsert, sizeof(struct dirent));
				bio_write(dataBlockNum, b);
				
				// Update metadata of the parent directory inode
				dir_inode.link++;
				dir_inode.size += sizeof(struct dirent);
				
				//free(temp); 
				break;	
			}
			//free(temp); 
		}
		//free(buffer);
	}

	// Writing the updated parent directory to the inode section
	writei(dir_inode.ino, &dir_inode);
	
	//printf("SUCCESS [dir_add] Added dirent with name: %s, to the parent directory with ino: %d\n", fname, dir_inode.ino);
	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {	
	int i;  
  	for (i = 0; i < 16; i++) {
		int dataBlockNum = dir_inode.direct_ptr[i];
		if (dataBlockNum < sb->d_start_blk || (dataBlockNum > (sb->d_start_blk + sb->max_dnum))) {
			// Invalid data block num
			// printf("ERROR [dir_remove] found an invalid data block num for this directory\n", dir_inode->ino, fname);
			continue;
		}
		// Read this data block into buffer
		void * block = malloc(BLOCK_SIZE);
		bio_read(dataBlockNum, block);
		
		struct dirent * tempDirent = malloc(sizeof(struct dirent));
		int j;
		for (j = 0; j < 16; j++) {
			// Read a dirent into tempDirent
			int dataBlockOffset = j * sizeof(struct dirent);
			memcpy(tempDirent, block + dataBlockOffset, sizeof(struct dirent));
			//printf("[dir_remove] tempdirent name: %s, ino: %d\n", tempDirent->name, tempDirent->ino);
			if (strcmp(tempDirent->name, fname) == 0 && tempDirent->valid == 1) {	
				// Free this dirent in the parent directory's data block
				//printf("[dir_remove] FOUND REMOVAL, removing %s, ino: %d, blockNum: %d\n", tempDirent->name, tempDirent->ino, dataBlockNum);
				dir_inode.size -= sizeof(struct dirent);
				void * buf = malloc(BLOCK_SIZE);
				bio_read(dataBlockNum, buf);
				struct dirent * newTemp = malloc(sizeof (struct dirent));
				newTemp->valid = 0;
				newTemp->ino = 0;
				char newName[252] = "";
				strcpy(newTemp->name, newName);
				memcpy(buf + dataBlockOffset, newTemp, sizeof(struct dirent));
				bio_write(dataBlockNum, newTemp);
				struct dirent * t = malloc(sizeof(struct dirent));

				// Check if this data block of the parent dir can be freed up - block num set to 0
				if(dir_inode.size % BLOCK_SIZE == 0) {
					printf("[dir_remove] parent data block iss able to be free up");
					void * bitmap_buffer = malloc(BLOCK_SIZE);
					dir_inode.direct_ptr[dataBlockNum] = 0;
					bio_read(sb->d_bitmap_blk, bitmap_buffer);
					unset_bitmap((bitmap_t)bitmap_buffer, dataBlockNum);
					bio_write(sb->d_bitmap_blk, bitmap_buffer);
				}
								
				// Update the metadata of the parent directory
				dir_inode.link--;
				writei(dir_inode.ino, &dir_inode);
				return 0;		
			}
		}
    }	
    // This fname didn't exist in this directory
	return -1;
}

int get_node_by_path(const char * path, uint16_t ino, struct inode * inode) {
	char * token = strdup(path);
	char * rest;
	struct dirent * target = malloc(sizeof(struct dirent));
	token = strtok_r(token, "/", &rest);
	if (token == NULL) {
		readi(ino, inode);
		return 0;
	}
	int check = dir_find(ino, token, strlen(token), target);
	if (check == 0) {
		return get_node_by_path(rest, target->ino, inode);
	}
	else {
		return -1;
	}
}

/* 
 * Make file system
 */
int tfs_mkfs() {
	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);
	
	// write superblock information in memory
	sb = malloc(sizeof(struct superblock));
	sb->magic_num = MAGIC_NUM;
	sb->max_inum = MAX_INUM;
	sb->max_dnum = MAX_DNUM;		
	sb->i_bitmap_blk = 1;
	sb->d_bitmap_blk = 2;
	sb->i_start_blk = 3;
	sb->d_start_blk = 68;
	
	// write superblock to disk
	bio_write(0, sb);
	
	// initialize bitmaps
	unsigned char inode_bitmap[MAX_INUM/8];
	memset(inode_bitmap, 0, MAX_INUM/8);
	unsigned char data_bitmap[MAX_DNUM/8];
	memset(data_bitmap, 0, MAX_DNUM/8);
	
	// update bitmap information for root directory
	set_bitmap(inode_bitmap, 0);
	set_bitmap(data_bitmap, 0);
	
	// Write bitmaps to disk
	bio_write(1, inode_bitmap);
	bio_write(2, data_bitmap);

	// creating the root directory inode
	struct inode * rootDirInode = malloc(sizeof(struct inode));
	rootDirInode->ino = 0;
	rootDirInode->valid = 1;
	
	// for the 2 initial dirents inserted to the root
	rootDirInode->size = 2 * sizeof(struct dirent);				
	rootDirInode->type = DIRTYPE;				
	rootDirInode->link = 2;
	rootDirInode->direct_ptr[0] = 68;
	
	// creating a root directory dirent for itself
	struct dirent * rootDirentSelf = malloc(sizeof(struct dirent));
	rootDirentSelf->ino = rootDirInode->ino;
	rootDirentSelf->valid = 1;
	strcpy(rootDirentSelf->name, ".");
	
	// creating a root directory parent dirent - (also itself in this case)
	struct dirent * rootDirentParent = malloc(sizeof(struct dirent));
	rootDirentParent->ino = rootDirInode->ino;
	rootDirentParent->valid = 1;
	strcpy(rootDirentParent->name, "..");

	// Writing the root inode to the disk
	void * buffer = malloc(BLOCK_SIZE);
	bio_read(sb->i_start_blk, buffer);
	memcpy(buffer, rootDirInode, sizeof(struct inode));
	bio_write(sb->i_start_blk, buffer);
	
	memset(buffer, 0, BLOCK_SIZE);
	memcpy(buffer, rootDirentSelf, sizeof(struct dirent));	
	memcpy(buffer + sizeof(struct dirent), rootDirentParent, sizeof(struct dirent));
	bio_write(rootDirInode->direct_ptr[0], buffer);

	free(buffer);
	return 0;
}


/* 
 * FUSE file operations
 */
static void * tfs_init(struct fuse_conn_info *conn) {
	// Step 1a: If disk file is not found, call mkfs
	int diskFileFound = dev_open(diskfile_path);
  	// Step 1b: If disk file is found, just initialize in-memory data structures
	if (diskFileFound == 0) {
		// read from the superblock in the disk file, initialize in memory data structures	
		void * buffer = malloc(BLOCK_SIZE);
		bio_read(0, buffer);
		sb = (struct superblock *) buffer;
		if (sb->magic_num != MAGIC_NUM) {
			fprintf(stderr, "magicnum does not match for the diskfile.\n");
			exit(EXIT_FAILURE);
		}
	} else {
		// call mkfs
		tfs_mkfs();
	}
	return NULL;
}

static void tfs_destroy(void *userdata) {
	// Step 1: De-allocate in-memory data structures
	free(sb);
	// Step 2: Close diskfile
	dev_close();
}

static int tfs_getattr(const char *path, struct stat *stbuf) {
	// Step 1: call get_node_by_path() to get inode from path	
	struct inode * node = malloc(sizeof(struct inode));
	int result = get_node_by_path(path, 0, node);
	
	if (result == -1) {
		return -ENOENT;	
	}
		
	if (node->type == DIRTYPE) {
		//printf("[getattr] inode type is a directory, path: %s\n", path);
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = node->link;
		stbuf->st_size = node->size;
	}
	else if (node->type == FILETYPE) {
		//printf("[getattr] inode type is a file, path: %s\n", path);
		stbuf->st_mode = S_IFREG | 0644;
		stbuf->st_nlink = node->link;
		stbuf->st_size = node->size;
	}
	
	stbuf->st_uid = getuid();
	stbuf->st_gid = getgid(); 
	time(&stbuf->st_mtime);
    time(&stbuf->st_atime);
	return 0;
}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode * node = malloc(sizeof(struct inode));
	int result = get_node_by_path(path, 0, node);
	// Step 2: If not find, return -1
    return result;
}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode * node = malloc(sizeof(struct inode));
	int success = get_node_by_path(path, 0, node);

	if (success == -1 || node->type != DIRTYPE) {
		return -1;
	}

	int done = -1;
	// Step 2: Read directory entries from its data blocks, and copy them to filler
	int i;  
	int count = 0;
  	for (i = 0; i < 16; i++) {
		if (done == 1) {
			break;
		}
		int data_block = node->direct_ptr[i];
		void * b = malloc(BLOCK_SIZE);
		bio_read(data_block, b);
		struct dirent * d = malloc(sizeof(struct dirent));
		int j;
		for (j = 0; j < 16; j++) {
			count++;
			if (count > node->link) {
				break;
			}
			memcpy(d, b + (j * sizeof(struct dirent)), sizeof(struct dirent));

			if (d->valid == 1) {
				if (filler(buffer, d->name, NULL, 0) != 0) {
					return 0;
				}
			}
		}
		free(b); 
  	 }
	 
	return 0;
}

static int tfs_mkdir(const char *path, mode_t mode) {
	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	char * parentDir = dirname(strdup(path));
	char * targetDir = basename(strdup(path));

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode * parentInode = malloc(sizeof (struct inode));
	int getNodeResult = get_node_by_path(parentDir, 0, parentInode);
	if (getNodeResult == -1) {
		return -1;	
	}
	
	// Step 3: Call get_avail_ino() to get an available inode number
	uint16_t ino = (uint16_t)get_avail_ino();
	
	if (ino == -1) {
		// No available inodes
		return -1;
	}
	
	// Step 4: Call dir_add() to add directory entry of target directory to parent directory	
	int dirAddResult = dir_add(*parentInode, ino, targetDir, strlen(targetDir));
	if (dirAddResult == -1) {
		// Unable to add a new dirent to this parent directory
		return -1;
	}
	
	// Step 5: Update inode for target directory
	struct inode * newDirInode = malloc(sizeof(struct inode));
	newDirInode->ino = ino;
	newDirInode->valid = 1;		
	newDirInode->size = 2 * sizeof(struct dirent);	
	newDirInode->type = DIRTYPE;
	newDirInode->link = 2;
	newDirInode->vstat.st_mtime = time(0);
	newDirInode->vstat.st_atime = time(0);

	int dataBlock = get_avail_blkno();
	if (dataBlock == -1) {
		// Not enough data block space to create this directory - need 2 dirents at least: '.' and '..'
		return -1;
	}
	
	// Set the initial data block
	newDirInode->direct_ptr[0] = sb->d_start_blk + dataBlock;
	
	// Set the bitmap for this new data block
	void * dbuf = malloc(BLOCK_SIZE);
	bio_read(sb->d_bitmap_blk, dbuf);
	set_bitmap((bitmap_t)dbuf, newDirInode->direct_ptr[0]);
	
	// creating a dirent for itself
	struct dirent * direntSelf = malloc(sizeof(struct dirent));
	direntSelf->ino = ino;
	direntSelf->valid = 1;
	strcpy(direntSelf->name, ".");
	
	// creating a parent dirent
	struct dirent * direntParent = malloc(sizeof(struct dirent));
	direntParent->ino = parentInode->ino;
	direntParent->valid = 1;
	strcpy(direntParent->name, "..");
	
	// Write the new dirents to the buffer
	memset(dbuf, 0, BLOCK_SIZE);
	memcpy(dbuf, direntSelf, sizeof(struct dirent));	
	memcpy(dbuf + sizeof(struct dirent), direntParent, sizeof(struct dirent));
	
	// Write the dirent buffer to the datablock
	bio_write(newDirInode->direct_ptr[0], dbuf);
	
	// Step 6: Call writei() to write inode to disk
	writei(ino, newDirInode);
	
	return 0;
}

static int tfs_rmdir(const char *path) {
	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	char * parentDir = dirname(strdup(path));
	char * targetDir = basename(strdup(path));
		
	// Step 2: Call get_node_by_path() to get inode of target directory
	struct inode * targetNode = malloc(sizeof(struct inode));
	int success = get_node_by_path(targetDir, 0, targetNode);
	
	if (success == -1 || targetNode->type != DIRTYPE || targetNode->link > 2) {
		// Not a valid directory to remove
		return -1;
	}
	
	// Step 3: Clear data block bitmap of target directory
	unsigned char buf[BLOCK_SIZE];
	bio_read(sb->d_bitmap_blk, buf);
	int i;
	for (i = 0; i < 16; i++) {
		if (targetNode->direct_ptr[i] < sb->d_start_blk || (targetNode->direct_ptr[i] > (sb->d_start_blk + sb->max_dnum))) {
			// Not a valid data block
			continue;
		}
		unset_bitmap((bitmap_t)buf, targetNode->direct_ptr[i] - sb->d_start_blk);
	}
	bio_write(sb->d_bitmap_blk, buf);
	
	// Step 4: Clear inode bitmap
	unsigned char buf2[BLOCK_SIZE];
	bio_read(sb->i_bitmap_blk, buf2);
	unset_bitmap((bitmap_t)buf2, targetNode->ino);
	bio_write(sb->i_bitmap_blk, buf2);

	// Clear inode data block
	void * buffer = malloc(BLOCK_SIZE);
	bio_read(targetNode->direct_ptr[0], buffer);
	memset(buffer, 0, BLOCK_SIZE);
	bio_write(targetNode->direct_ptr[0], buffer);

	// Clear inode block
	struct inode * temp = malloc(sizeof(struct inode));
	memset(temp, 0, sizeof(struct inode));
	writei(targetNode->ino, temp);
	
	// Step 5: Call get_node_by_path() to get inode of parent directory
	struct inode * parentNode = malloc(sizeof(struct inode));
	get_node_by_path(parentDir, 0, parentNode);
	
	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
	dir_remove(*parentNode, targetDir, strlen(targetDir));
	return 0;
}

static int tfs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {		
	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char * parentDirPath = dirname(strdup(path));
	char * targetFileName = basename(strdup(path));
	
	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode * dir_inode = malloc(sizeof(struct inode));
	int success = get_node_by_path(parentDirPath, 0, dir_inode);
	if (success == -1) {
		return -1;	
	}
	// Step 3: Call get_avail_ino() to get an available inode number
	uint16_t ino = (uint16_t)get_avail_ino();
	
	// Step 4: Call dir_add() to add directory entry of target file to parent directory
	int dirAddResult = dir_add(*dir_inode, ino, targetFileName, strlen(targetFileName));
	if(dirAddResult == -1){
		printf("[tfs_create] dir_add failed, returning -1\n");
		return -1;
	}
	
	// Step 5: Update inode for target file
	struct inode * file_inode = malloc(sizeof(struct inode));
	file_inode->ino = ino;
	file_inode->valid = 1;		
	file_inode->size = 0;	
	file_inode->type = FILETYPE;
	file_inode->link = 1;
	file_inode->vstat.st_mtime = time(0);
	file_inode->vstat.st_atime = time(0);
	
	// zero out all the data block info
	int i = 0;
	for (i = 0; i < 16; i++) {
		file_inode->direct_ptr[i] = 0;
	}
	for (i = 0; i < 8; i++) {
		file_inode->indirect_ptr[i] = 0;
	}
	
	// Step 6: Call writei() to write inode to disk
	writei(ino, file_inode);
	return 0;
}

static int tfs_open(const char *path, struct fuse_file_info *fi) {
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode * node = malloc(sizeof(struct inode));
	int success = get_node_by_path(path, 0, node);
	// Step 2: If not find, return -1
	return success;
}
static int isDataBitSet(int i) {
	void * checkBuffer = malloc(BLOCK_SIZE);
	bio_read(sb->d_bitmap_blk, checkBuffer);
	int isSet = get_bitmap((bitmap_t)checkBuffer, i);
	free(checkBuffer);
	return isSet;
}

static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode * node = malloc(sizeof(struct inode));
	int success = get_node_by_path(path, 0, node);
	if (success == -1) {
		return -1;	
	}
	
	//Determine if there is enough space after the offset to read from
	int numBlocksToRead = (size / BLOCK_SIZE) + 1;
	int startBlock = offset / BLOCK_SIZE;
	//if((MAX_BLOCKS_PER_INODE)-startBlock < numBlocksToRead){
	//	printf("[tfs_read] ERROR: Attempting to read more blocks than an inode can contain\n");
	//	return -1;
	//}
	
	int indirect = 0;
	if(startBlock >= 16){
		indirect = 1;
		startBlock -= 16;
	}
	int blockOffset = offset % BLOCK_SIZE;
	
	int bytesWritten = 0;
	while (bytesWritten < size && indirect == 0) {
		int blockNum = node->direct_ptr[startBlock];
		if (blockNum < sb->d_start_blk || (blockNum > (sb->d_start_blk + sb->max_dnum))) {
			printf("[tfs_read] ERROR: Attempting to read from an unallocated block\n");
			return -1;
		}
		void * blockBuffer = malloc(BLOCK_SIZE);
		bio_read(blockNum, blockBuffer);
	
	
		if (bytesWritten == 0) {
			if (blockOffset + size < BLOCK_SIZE) {
				memcpy(buffer, blockBuffer + blockOffset, size);
				bytesWritten += size;
			} else {
				memcpy(buffer, blockBuffer + blockOffset, BLOCK_SIZE - blockOffset);
				blockOffset = 0;		
				bytesWritten += BLOCK_SIZE - blockOffset;
			}
		} else {
			int amountLeft = size - bytesWritten;
			if (amountLeft > BLOCK_SIZE) {
				// Write a full block from 0
				memcpy(buffer + bytesWritten, blockBuffer, BLOCK_SIZE);
				bytesWritten += BLOCK_SIZE;
			} else {
				// Write from 0 up to a partial block
				memcpy(buffer + bytesWritten, blockBuffer, amountLeft);
				bytesWritten += amountLeft;
			}	
		}
		
		startBlock++;
		if(startBlock >= 16){
			indirect = 1;
			startBlock -= 16;
		}
	}
	
	while (bytesWritten < size) {
		int indirect_num = startBlock / (BLOCK_SIZE / sizeof(int));
		int indirect_ptr_num = node->indirect_ptr[indirect_num];

		if (indirect_ptr_num < sb->d_start_blk || (indirect_ptr_num > (sb->d_start_blk + sb->max_dnum))) {
			printf("[tfs_read] ERROR: Attempting to read from an unallocated block\n");
			return -1;
		}
		
		// Read this INDIRECT block from disk
		void * indirect_ptr_block = malloc(BLOCK_SIZE);
		bio_read(indirect_ptr_num, indirect_ptr_block);
		
		int block_num = startBlock % (BLOCK_SIZE / sizeof(int));
		int data_block_num = 0;
		memcpy(&data_block_num, indirect_ptr_block + (block_num * sizeof(int)), sizeof(int));
		
		if (data_block_num < sb->d_start_blk || (data_block_num > (sb->d_start_blk + sb->max_dnum))) {
			printf("[tfs_read] ERROR: Attempting to read from an unallocated block\n");
			return -1;
		}
		
		void * data_block = malloc(BLOCK_SIZE);
		bio_read(data_block_num, data_block);
		
		if (bytesWritten == 0) {
			if (blockOffset + size < BLOCK_SIZE) {
				memcpy(buffer, data_block + blockOffset, size);
				bytesWritten += size;
			} else {
				memcpy(buffer, data_block + blockOffset, BLOCK_SIZE - blockOffset);
				blockOffset = 0;		
				bytesWritten += BLOCK_SIZE - blockOffset;
			}
		} else {
			int amountLeft = size - bytesWritten;
			if (amountLeft > BLOCK_SIZE) {
				// Write a full block from 0
				memcpy(buffer + bytesWritten, data_block, BLOCK_SIZE);
				bytesWritten += BLOCK_SIZE;
			} else {
				// Write from 0 up to a partial block
				memcpy(buffer + bytesWritten, data_block, amountLeft);
				bytesWritten += amountLeft;
			}	
		}
		
		blockOffset = 0;	
		startBlock++;
	}
	free(node);
	return size;
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Note: this function should return the amount of bytes you write to disk
	// Step 1: You could call get_node_by_path() to get inode from path	
	struct inode * node = malloc(sizeof(struct inode));
	
	int success = get_node_by_path(path, 0, node);
	if (success == -1) {
		printf("[tfs_write] ERROR: Failed to get inode\n");
		return -1;	
	}

	//Determine if there is enough space after the offset
	int numBlocksToWrite = (size / BLOCK_SIZE) + 1;
	int startBlock = offset / BLOCK_SIZE;
	if((MAX_BLOCKS_PER_INODE) - startBlock < numBlocksToWrite){
		printf("[tfs_write] ERROR: Attempting to write more blocks than an inode can contain\n");
		return -1;
	}
	
	int indirect = 0;
	if(startBlock >= 16) {
		indirect = 1;
		startBlock -= 16;
	}
	
	int blockOffset = offset % BLOCK_SIZE;
	int bytesWritten = 0;
	
	while (bytesWritten < size && indirect == 0) {
		int blockNum = node->direct_ptr[startBlock];
		if (blockNum < sb->d_start_blk || (blockNum > (sb->d_start_blk + sb->max_dnum))) {
			blockNum = get_avail_blkno();

			if(blockNum == -1){
				printf("[tfs_write] ERROR: No more data blocks available\n");
				return -1;
			}
			blockNum += sb->d_start_blk;
			int amountLeft = size - bytesWritten;
			if (amountLeft > BLOCK_SIZE) {
				node->size += BLOCK_SIZE;
			} else {
				node->size += amountLeft;
			}
			
			node->direct_ptr[startBlock] = blockNum;
		} 
		void * blockBuffer = malloc(BLOCK_SIZE);
		bio_read(blockNum, blockBuffer);
		
		if (bytesWritten == 0) {
			if (blockOffset + size < BLOCK_SIZE) {
				memcpy(blockBuffer + blockOffset, buffer, size);
				bytesWritten += size;
			} else {
				memcpy(blockBuffer + blockOffset, buffer, BLOCK_SIZE - blockOffset);
				blockOffset = 0;		
				bytesWritten += BLOCK_SIZE - blockOffset;
			}
		} else {
			int amountLeft = size - bytesWritten;
			if (amountLeft > BLOCK_SIZE) {
				// Write a full block from 0
				memcpy(blockBuffer, buffer + bytesWritten, BLOCK_SIZE);
				bytesWritten += BLOCK_SIZE;
			} else {
				// Write from 0 up to a partial block
				memcpy(blockBuffer, buffer + bytesWritten, amountLeft);
				bytesWritten += amountLeft;
			}	
		}		
		
		bio_write(blockNum, blockBuffer);
		startBlock++;
		if(startBlock >= 16 && indirect == 0){
			indirect = 1;
			startBlock -= 16;
		}
	}
	
	while (bytesWritten < size) {
		int indirect_num = startBlock / (BLOCK_SIZE / sizeof(int));
		int indirect_ptr_num = node->indirect_ptr[indirect_num];

		if (indirect_ptr_num < sb->d_start_blk || (indirect_ptr_num > (sb->d_start_blk + sb->max_dnum))) {
			indirect_ptr_num = get_avail_blkno();

			if(indirect_ptr_num == -1) {
				printf("[tfs_write] ERROR: No more data blocks available\n");
				return -1;
			}
			indirect_ptr_num += sb->d_start_blk;
			node->indirect_ptr[indirect_num] = indirect_ptr_num;
			
			// Set all the entries of this new indirect block to 0
			void * temp = malloc(BLOCK_SIZE);
			bio_read(node->indirect_ptr[indirect_num], temp);
			memset(temp, 0, BLOCK_SIZE);
			bio_write(node->indirect_ptr[indirect_num], temp);
			free(temp);
		} 
		
		// Read this INDIRECT block from disk
		void * indirect_ptr_block = malloc(BLOCK_SIZE);
		bio_read(indirect_ptr_num, indirect_ptr_block);
		
		int block_num = startBlock % (BLOCK_SIZE / sizeof(int));
		int data_block_num = 0;
		memcpy(&data_block_num, indirect_ptr_block + (block_num*sizeof(int)), sizeof(int));
		
		if (data_block_num < sb->d_start_blk || (data_block_num > (sb->d_start_blk + sb->max_dnum))) {
			data_block_num = get_avail_blkno();

			if(data_block_num == -1){
				printf("[tfs_write] ERROR: No more data blocks available\n");
				return -1;
			}
			data_block_num += sb->d_start_blk;
			int amountLeft = size - bytesWritten;
			if (amountLeft > BLOCK_SIZE) {
				node->size += BLOCK_SIZE;
			} else {
				node->size += amountLeft;
			}
			memcpy(indirect_ptr_block + (block_num*sizeof(int)), &data_block_num, sizeof(int));
			//printf("[tfs_write] added dataBlockNum: %d to indirectPtr: %d, block_num: %d\n",data_block_num, indirect_ptr_num, block_num);
		}
		
		// Update the array of pointers
		bio_write(indirect_ptr_num, indirect_ptr_block);
		
		// Get a data block from of the indirect block
		void * data_block = malloc(BLOCK_SIZE);
		bio_read(data_block_num, data_block);
		
		if (bytesWritten == 0) {
			if (blockOffset + size < BLOCK_SIZE) {
				memcpy(data_block + blockOffset, buffer, size);
				bytesWritten += size;
			} else {
				memcpy(data_block + blockOffset, buffer, BLOCK_SIZE - blockOffset);
				blockOffset = 0;		
				bytesWritten += BLOCK_SIZE - blockOffset;
			}
		} else {
			int amountLeft = size - bytesWritten;
			if (amountLeft > BLOCK_SIZE) {
				// Write a full block from 0
				memcpy(data_block, buffer + bytesWritten, BLOCK_SIZE);
				bytesWritten += BLOCK_SIZE;
			} else {
				// Write from 0 up to a partial block
				memcpy(data_block, buffer + bytesWritten, amountLeft);
				bytesWritten += amountLeft;
			}	
		}
		
		blockOffset = 0;
		bio_write(data_block_num, data_block);
		startBlock++;
	}
	
	writei(node->ino, node);
	
	free(node);
	// Note: this function should return the amount of bytes you write to disk
	return size;
}


static int tfs_unlink(const char *path) {
	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char * parentDir = dirname(strdup(path));
	char * targetFile = basename(strdup(path));

	// Step 2: Call get_node_by_path() to get inode of target directory
	struct inode * curr_inode = malloc(sizeof(struct inode));
	if (get_node_by_path(path, 0, curr_inode) != 0 || curr_inode->type != FILETYPE){
		return -1;
	}

	unsigned char bitmap_buf[BLOCK_SIZE] = {};
	char data_buf[BLOCK_SIZE] = {};
	int i;

	// Step 3: Clear data block bitmap of target directory
	bio_read(sb->d_bitmap_blk, bitmap_buf);
	for (i = 0; i < 16; i++) {
		int data_block = curr_inode->direct_ptr[i];
		if (data_block < sb->d_start_blk || (data_block > (sb->d_start_blk + sb->max_dnum))) {
			continue;
		}
		bio_read(data_block, data_buf);
		memset(data_buf, 0, BLOCK_SIZE);
		bio_write(data_block, data_buf);
		curr_inode->direct_ptr[i] = 0;
		unset_bitmap((bitmap_t)bitmap_buf, data_block - sb->d_start_blk);
	}
	
	// Clear INDIRECT ptr bitmaps and blocks
  	for (i = 0; i < 8; i++) {
		int indirect_block_num = curr_inode->indirect_ptr[i];

		if (indirect_block_num < sb->d_start_blk || (indirect_block_num > (sb->d_start_blk + sb->max_dnum))) {
			break;
		}
		
		// Read this INDIRECT block from disk
		void * indirect_block = malloc(BLOCK_SIZE);
		bio_read(indirect_block_num, indirect_block);

		curr_inode->indirect_ptr[i] = 0;
		// Unset the bitmap for this INDIRECT data block
		unset_bitmap((bitmap_t)bitmap_buf, indirect_block_num - sb->d_start_blk);
		
		// For each of the indirect blocks in the indirect pointer block: unset bitmaps and clear valid bits, write those blocks back to memory
		int numOfIndirectBlocks = BLOCK_SIZE / sizeof(int);
		int j;
		for (j = 0; j < numOfIndirectBlocks; j++) {

			int data_block = 0;
			memcpy(&data_block, indirect_block + (j * sizeof(int)), sizeof(int));

			if (data_block < sb->d_start_blk || (data_block > (sb->d_start_blk + sb->max_dnum))) {
				break;
			}
			void * buffer = malloc(BLOCK_SIZE);
			bio_read(data_block, buffer);
			
			// Unset the bitmap for this data block
			unset_bitmap((bitmap_t)bitmap_buf, data_block - sb->d_start_blk);

			// Clear the valid bits for these data blocks
			memset(buffer, 0, BLOCK_SIZE);

			// Write the data block back to memory
			bio_write(data_block, buffer);

		}

		// Write the data block back to memory
		bio_write(indirect_block_num, indirect_block);
	}
	
	// Write the data block bitmap back to disk
	bio_write(sb->d_bitmap_blk, bitmap_buf);
	
	// Step 4: Clear inode bitmap and its data block
	curr_inode->valid = 0;
	curr_inode->type = 0;
	curr_inode->link = 0;
	
	bio_read(sb->i_bitmap_blk, bitmap_buf);
	unset_bitmap((bitmap_t)bitmap_buf, curr_inode->ino);
	bio_write(sb->i_bitmap_blk, bitmap_buf);

	// Step 5: Call get_node_by_path() to get inode of parent directory
	if (get_node_by_path(parentDir, 0, curr_inode) != 0 || curr_inode->type != DIRTYPE) {
		return -1;
	}

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
	dir_remove(*curr_inode, targetFile, strlen(targetFile));
	return 0;
}

static int tfs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int tfs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static struct fuse_operations tfs_ope = {
	.init		= tfs_init,
	.destroy	= tfs_destroy,

	.getattr	= tfs_getattr,
	.readdir	= tfs_readdir,
	.opendir	= tfs_opendir,
	.releasedir	= tfs_releasedir,
	.mkdir		= tfs_mkdir,
	.rmdir		= tfs_rmdir,

	.create		= tfs_create,
	.open		= tfs_open,
	.read 		= tfs_read,
	.write		= tfs_write,
	.unlink		= tfs_unlink,

	.truncate   = tfs_truncate,
	.flush      = tfs_flush,
	.utimens    = tfs_utimens,
	.release	= tfs_release
};

int main(int argc, char *argv[]) {
	int fuse_stat;
	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");
	fuse_stat = fuse_main(argc, argv, &tfs_ope, NULL);
	return fuse_stat;
}

