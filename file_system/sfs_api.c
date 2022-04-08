#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include "disk_emu.h"
#include "sfs_api.h"

//variable for super block, root directory, inode table, open table, bitmap and current file
block super_block;
root_entry directory[100];
inode inode_table [100];
fdt_entry open_fdt[100];
bool bitmap [4096];
int curr_file = 0;

void mksfs(int fresh){
	if(fresh){
		//init new disk with block size 1024 and 4096 blocks;
		init_fresh_disk("sfs.disk", 1024, 4096);
		//init the super block 
		super_block.magic = 1;
		super_block.block_size = 1024;
		super_block.file_sys_size = 1024;
		super_block.inode_table_length = 100;
		super_block.root_dir = 0;
		//write it at address 0 for 1 block
		write_blocks( 0 , 1 , &super_block);

		//init all inodes and set the unused pointers to 0
		for(int i = 0;i<100;i++){
			//set all modes, == false for free, true for occupied
            inode_table[i].mode = 0;
            inode_table[i].link_cnt = 0;
            inode_table[i].uid = 0;
            inode_table[i].gid = 0;
            inode_table[i].size = 0;
			for (int j=0;j<12;j++){
				inode_table[i].pointer[j] = 0;
			}
            inode_table[i].ind_pointer = 0;
        }
        //write it at address 1 for 8 block
		write_blocks(1, 8, &inode_table);
		
		//init open table and set the unused pointers and inode number to -1
		for(int i = 0 ; i < 100 ; i ++){
			open_fdt[i].inode_num = -1;
			open_fdt[i].rw_ptr = -1;
		}

		//init directory and bitmap
		memset(&directory, 0, sizeof(directory));
		write_blocks(4090, 2, &directory);
		write_blocks(4092, 4, &bitmap);

	}else{
		//read all from the disk
		init_disk("sfs.disk", 1024, 4096);
		read_blocks(0, 1 , &super_block);
        read_blocks(1 ,8 , &inode_table);
        read_blocks(4090,2,&directory);
        read_blocks(4092, 4, &bitmap);
	}
}

int sfs_fopen(char *name){
    //return -1 if the file name is too lang
	if (strlen(name) > MAXFILENAME) {
        return -1;
    }
	//check the file int the directory
	for (int i = 0 ; i < 100; i++){
		int num = directory[i].inode_num;
		//if find it, then the file is in the directories
		if ( strcmp (directory[i].filename, name)==0){
            //if it not in the open table, then init it. ow return the num
			if(open_fdt[num].inode_num == -1){
				open_fdt[num].rw_ptr = 0;
				open_fdt[num].inode_num = num;		
			}
			return num;
		}
	}

	//if not in directory, create a new file
	for (int i = 0 ; i < 100 ; i++){
		if (strcmp(directory[i].filename,"")==0){
			directory[i].inode_num = i;
			open_fdt[i].inode_num = i;
			memcpy(directory[i].filename, name , MAXFILENAME);
			
			inode_table[i].size = 0;
			open_fdt[i].rw_ptr = 0;
            //update the block
			write_blocks (4090, 2, &directory);
			write_blocks( 1 , 8 , &inode_table);
			return i;
		}
	}
	return -1;
}

int allocate_block () {
	//find free block from 9 to 4090
    //the other blocks are used for store info
	int index;
	for (index = 9; index < 4090; index++) {
		if (bitmap[index] == false) {
			bitmap[index] = true;
			break;
		}
	}
    //if index is 4090 there is no avaliable space 
	if (index == 4090) {
		printf("No space available\n");
		return -1;
	}
    //update the block
	write_blocks (4092, 4, &bitmap);
	return index;
}


int apply_write_block(int fileID, int count){
    //if count less than 12 we use pointer o.w we use indirect pointer
	if (count < 12){
        //if the pointer not point to a block, allocate a block for it
		if(inode_table[fileID].pointer[count] == 0){
			inode_table[fileID].pointer[count] = allocate_block();
		}
		return inode_table[fileID].pointer[count];
	}else{
		count -= 12;
        // if the indirect point is used, we continue to check
		if(inode_table[fileID].ind_pointer!=0){
			int indirectBuffer [256] = {0};
        	read_blocks(inode_table[fileID].ind_pointer,1,indirectBuffer);
			if(indirectBuffer[count] == 0){
                //if the pointer not point to a block, allocate a block for it
				indirectBuffer[count] = allocate_block();
				write_blocks(inode_table[fileID].ind_pointer,1,indirectBuffer);
			}
			return indirectBuffer[count];
		}
		else{
            //if the indirect pointer not point to a block, allocate a block for it
			inode_table[fileID].ind_pointer = allocate_block();
			int indirectBuffer [256] = {0};
        	read_blocks(inode_table[fileID].ind_pointer,1,indirectBuffer);
            //if the pointer not point to a block, allocate a block for it
			indirectBuffer[count] = allocate_block();
			write_blocks(inode_table[fileID].ind_pointer,1,indirectBuffer);
			return indirectBuffer[count];
		}
	}
}

int sfs_fwrite(int fileID, char *buf, int length){
	for(int i = 0 ; i < 100 ; i++){
		if(i == fileID){
			//if the file is not open, return -1
			if(open_fdt[i].inode_num==-1){
				printf("Can not write into a closed file\n");
				return -1;
			}	
		}
	}

	int count = inode_table[fileID].size/1024;
	int offset = inode_table[fileID].size % 1024;
    //if count larger than 268, it means all blocks are used
	if(count >= 268){
		printf("Can not write too much bytes\n");
		return -1;
	}

	int start_ptr = open_fdt[fileID].rw_ptr;
    //check the remaining space for writing in the first block
	int byte_to_write = min(1024-offset, length);
	int isFirst = true;

	while(length > 0){
		int block = apply_write_block(fileID, count);
		if(isFirst){
			char tmp_buf [1024] = {0};
			read_blocks(block,1,tmp_buf);
			memcpy(tmp_buf + offset, buf, byte_to_write);
			write_blocks(block,1, tmp_buf);
			isFirst = false;
		}else{
			if(byte_to_write < 1024){
                //write for the last block
				char tmp_buf [1024] = {0};
				read_blocks(block,1,tmp_buf);
				memcpy(tmp_buf, buf, byte_to_write);
				write_blocks(block,1, tmp_buf);
			}else{
                //regular 1024size block
				write_blocks(block,1, buf);
			}
		}

		count++;
		buf += byte_to_write;
		open_fdt[fileID].rw_ptr += byte_to_write;
		length -= byte_to_write;
        //update the byte_to_write
		byte_to_write = min(1024, length);
	}
    //update the block
	inode_table[fileID].size = max(inode_table[fileID].size, open_fdt[fileID].rw_ptr);
	write_blocks( 1 , 8 , &inode_table);
    //set current file
	curr_file = fileID;
    //return all writen bytes
	return open_fdt[fileID].rw_ptr - start_ptr;
}

int apply_read_block(int fileID, int count){
    //if count less than 12 we use pointer o.w we check indirect pointer
	if (count < 12){
		return inode_table[fileID].pointer[count];
	}else{
		count -= 12;
		int indirectBuffer [256];
        read_blocks(inode_table[fileID].ind_pointer,1,indirectBuffer);
        return indirectBuffer[count];
	}
}

int sfs_fread(int fileID, char *buf, int length){
	//if the file is not open, return -1
	for(int i = 0 ; i < 100 ; i++){
		if(i == fileID){
			if(open_fdt[i].inode_num==-1){
				printf("Can not read a closed file\n");
				return -1;
			}
		}
	}
    //update the max bytes we can read
	length = min(length, inode_table[fileID].size - open_fdt[fileID].rw_ptr);

	int count = ((open_fdt[fileID].rw_ptr)/1024);
	int offset = open_fdt[fileID].rw_ptr % 1024;
    //if count larger than 268, it means all blocks are used
	if(count >= 268){
		printf("Can not read too much bytes\n");
		return -1;
	}
    //check how many we need to read
	int start_ptr = open_fdt[fileID].rw_ptr;
	int byte_to_read = min(1024-offset, length);
	bool isFirst = true;

	while(length > 0){
		int block = apply_read_block(fileID, count);
		if(isFirst){
			char tmp_buf[1024] = {0};
			read_blocks(block,1,tmp_buf);
			memcpy(buf,tmp_buf+offset, byte_to_read);
			isFirst = false;
		}else{
			if(byte_to_read < 1024){
                //the last block
				char tmp_buf [1024] = {0};
				read_blocks(block,1, tmp_buf);
				memcpy(buf, tmp_buf, byte_to_read);
			}else{	
                //regular 1024size block
				read_blocks(block,1, buf);
			}
		}
		count++;
		buf += byte_to_read;
		open_fdt[fileID].rw_ptr += byte_to_read;
		length -= byte_to_read;

		byte_to_read = min(1024, length);
	}
    //set the current file
	curr_file = fileID;
    //return bytes we read
	return open_fdt[fileID].rw_ptr - start_ptr;
}

int sfs_fseek(int fileID, int loc){
	for(int i = 0 ; i < 100 ; i++){
		if(i == fileID){
			//if find the file, set read/write ptr to the location
			open_fdt[i].rw_ptr = loc;
			return 0;
		}
	}
	return -1;
}

int sfs_fclose(int fileID){
	for(int i = 0 ; i < 128 ; i++){
		if(i == fileID){
			//if the file is open, we close it
			if(open_fdt[i].inode_num != -1){
				open_fdt[i].inode_num = -1;
				open_fdt[i].rw_ptr = -1;
				return 0;
			}else{
				return -1;
			}
			return 0;
		}
	}
	return -1;
}

int sfs_remove(char *file){
	for(int i = 0; i < 100 ; i ++){
		if(strcmp(file, directory[i].filename)==0){
			//if find the file, check if it is open
			int inode_num = directory[i].inode_num;
			if(open_fdt[inode_num].inode_num != 0){
                return 0;
			}
			//init the inode table
			inode_table[inode_num].mode = 0;
			inode_table[inode_num].gid = 0;
			inode_table[inode_num].link_cnt = 0;
			inode_table[inode_num].size = 0;
			inode_table[inode_num].uid = 0;
			for(int i = 0 ; i < 12 ; i ++){
                //init the pointer and free bitmap
				if(inode_table[inode_num].pointer[i] == 0){
					break;
				}else{
					bitmap[inode_table[inode_num].pointer[i]] = false;
					inode_table[inode_num].pointer[i] = 0;
				}
			}
			//init the indirect pointer and free bitmap
			if(inode_table[inode_num].ind_pointer != 0){
				int indirectBuffer [256];
        		read_blocks(inode_table[inode_num].ind_pointer,1,indirectBuffer);
				for(int i = 0 ; i < 256; i++){
                    //init the pointer and free bitmap
					if(indirectBuffer[i] == 0){
						break;
					}else{
						bitmap[indirectBuffer[i]] = false;
						indirectBuffer[i] = 0;
					}
				}
				bitmap[inode_table[inode_num].ind_pointer] = false;
				inode_table[inode_num].ind_pointer = 0;
			}
            //init the open table
			open_fdt[inode_num].inode_num = -1;
			open_fdt[inode_num].rw_ptr = -1;

            //init directory
			strcpy(directory[i].filename, "");
			directory[i].inode_num = -1;
            //update the blocks
			write_blocks (1 ,8 , &inode_table);
        	write_blocks(4090,2,&directory);
        	write_blocks (4092, 4, &bitmap);
			return 0;
		}
	}
	return 0;
}

int sfs_getfilesize(const char* path){
	for(int i = 0; i < 101; i ++){
		//if find the file, return the file size
		if ( strcmp(directory[i].filename, path)==0){
			int num = directory[i].inode_num;
			return inode_table[num].size;
		}
	}
	return -1;
}

int sfs_getnextfilename(char *fname){
	if(curr_file == 99){
		//reset current file and return 0
		curr_file = 0;
		return 0;
	}else{
		for(;curr_file < 100;curr_file++){
            //find next file and return it
			if(directory[curr_file].inode_num!=0){
				strcpy( fname, directory[curr_file].filename);
				curr_file++;
				return 1;
			}
		}
		return 0;
	}
}