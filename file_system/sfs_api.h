#ifndef SFS_API_H
#define SFS_API_H

// You can add more into this file.
#define MAXFILENAME 36
#define max(a,b) ((a) > (b) ? (a) : (b))
#define min(a,b) ((a) < (b) ? (a) : (b))

typedef struct block{
	int magic;
	int block_size;
	int file_sys_size;
	int inode_table_length;
	int root_dir;
} block;

typedef struct root_entry{
    int inode_num;
    char filename[40];
} root_entry;

typedef struct inode{
	int mode;
	int link_cnt;
	int uid;
	int gid;
	int size;
	int pointer[12];
	int ind_pointer;
} inode;

typedef struct fdt_entry {
	int inode_num;
	int rw_ptr;
} fdt_entry;

void mksfs(int);

int sfs_getnextfilename(char*);

int sfs_getfilesize(const char*);

int sfs_fopen(char*);

int sfs_fclose(int);

int sfs_fwrite(int, char*, int);

int sfs_fread(int, char*, int);

int sfs_fseek(int, int);

int sfs_remove(char*);

#endif