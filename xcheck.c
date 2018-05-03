#include "fs.h"
#include "types.h"
#include <sys/mman.h>


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sysinfo.h>
#include <sched.h>

#define TRUE 0
#define FALSE 1

int
main(int argc, char *argv[])
{
  char* map;
  int size;
  int fd, i;
  //Create an argument checker
  /*if(argc <= 1){
    cat(0);
    exit();
  }*/

  for(i = 1; i < argc; i++){
    if((fd = open(argv[i], O_RDONLY, (mode_t) 0600)) < 0){
      printf("image not found. \n");
      exit(1);
    }

    struct stat fileInfo = {0};

    if (fstat(fd, &fileInfo) == -1)
      {
	printf("pzip: error getting file size\n");
	return 1;
      }

    //empty file is NO ERROR
    /*
    if (fileInfo.st_size == 0)
      {
	printf("pzip: file is empty\n");
	return 0;
      }
    */
    
    
    size = fileInfo.st_size; 

    if (size > 0) { 
    map = mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
      close(fd);
      printf("pzip: error mmap\n");
      return 1;
    }
   
    struct superblock *sb;
    sb = (struct superblock *)(map + BSIZE);
	
    //inodes
    int i, j;
	int blockAddr, type;
	// First inode block in list of inodes
    struct dinode *dip = (struct dinode *)(map + 2*BSIZE);
	// Pointer to bitmap location
    void *bitmap = (void *)(map + (sb->ninodes/IPB + 3)*BSIZE);
    // List of used blocks
    int usedBlocks[sb->size];
	// Number of links to inode in file system
    int numLinks[sb->ninodes];
	// List of used inodes
    int inodeUsed[sb->ninodes];
	// Directory list for used inodes
    int usedinodeDirectory[sb->ninodes];
	
    // Set all used blocks to unused
    for(j = 0; j < sb->size; j++ ) {
			  usedBlocks[j] = 0;
	}
	// Set used blocks to used in list
    for (j = 0; j < (sb->ninodes/IPB + 3 + (sb->size/(BSIZE*8)+1)); j++) {
        usedBlocks[j] = 1;
    }

    for (i = 0; i < sb->ninodes; i++) {
		struct dirent *currDirectory;
		type = dip->type;
        int k;

		// Check 3: valid root inode
        if (i == 1 && type != 1) {
        	fprintf(stderr, "ERROR: root directory does not exist.\n");
            exit(1); 
        }
		// Check for valid file type
		if (type >= 0 && type <= 3) {
			// Make sure it is valid
			if (type != 0) {
				// If directory, check to make sure it is a valid directory
		        if (type == 1) {
		            void *blkAddr = map + dip->addrs[0] * BSIZE;
		            currDirectory = (struct dirent*)(blkAddr);
					// Check 4: valid directory format
		            if (strcmp(currDirectory->name, ".") != 0) {
		                fprintf(stderr, "ERROR: directory not properly formatted.\n");
		                exit(1);
		            }
		            currDirectory++;
		            if (strcmp(currDirectory->name, "..") != 0) {
		                fprintf(stderr, "ERROR: directory not properly formatted.\n");
		                exit(1);
		            }
		            // Check 5: valid parent directory
		            if (i != 1 && currDirectory->inum == i) {
		                fprintf(stderr, "ERROR: parent directory mismatch.\n");
		                exit(1);
		            }
		            struct dinode *parentDir = (struct dinode *)(map + 2*BSIZE + currDirectory->inum*sizeof(struct dinode));
		            if (parentDir->type != 1) {
		                fprintf(stderr, "ERROR: parent directory mismatch.\n");
		                exit(1);
		            }
		            int validParentDir = FALSE;
					int x, y, z;
		            for (x = 0; x < NDIRECT; x++) {
		                struct dirent *currDirectory;
		                if (parentDir->addrs[x] != 0) {
							currDirectory = (struct dirent *)(map + parentDir->addrs[x]*BSIZE);
							// Check for valid parent directory
			                for (y = 0; y < BSIZE/sizeof(struct dirent); y++) {
								if (currDirectory->inum != i) { 
									validParentDir = TRUE;
								}
								currDirectory++;
			                }
						}
		                if (dip->addrs[x] != 0) {
			                currDirectory = (struct dirent *)(map + dip->addrs[x]*BSIZE);
							// Find used inodes and mark them in list
			                for (z = 0; z < BSIZE/sizeof(struct dirent *); x++) {
			                    if (currDirectory->inum == 0) {
									break;
								}
			                    usedinodeDirectory[currDirectory->inum] = 1;
								// For each used inode found also increment reference count
			                    if (strcmp(currDirectory->name,".") != 0 && strcmp(currDirectory->name,"..") != 0) {
			                        numLinks[currDirectory->inum]++;
			                    }
			                    currDirectory++;
			                }
		                }
		            }
					// Check 5: valid parent directory
		            if (validParentDir == FALSE) {
		                fprintf(stderr, "ERROR: parent directory mismatch.\n");
		                exit(1);
		            }
		        }
				
		        for (k = 0; k < NDIRECT+1; k++) {
					int x;
					blockAddr = dip->addrs[k];
					// Make sure address is valid
		            if (blockAddr != 0) {
						if (blockAddr != 0) {
			            	// Check 2: bad address in inode
				            if((blockAddr) < ((int)BBLOCK(sb->nblocks, sb->ninodes))+1 || blockAddr > (sb->size * BSIZE)){
				                fprintf(stderr, "ERROR: bad direct address in inode.\n");
				                exit(1);
				            }
							// Check 8: check used blocks are only used once
							if(usedBlocks[blockAddr] == 1){
								fprintf(stderr, "ERROR: address used more than once.\n");
								exit(1);
							}
						}
		                usedBlocks[blockAddr] = 1;
					
			            // Check 6: check used blocks in bitmap
			            int bitmapLocation = (*((char*)bitmap + (blockAddr >> 3)) >> (blockAddr & 7)) & 1;
			            if (bitmapLocation == 0) {
			                fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
			                exit(1);
			            }
					}
					
					if(type == 1 && blockAddr > 1 && k == 0){
						int checkDirFormat = 0;
						int isRoot = 0;
						int checkRootDir = 0;
						if (i == 1) {
							isRoot++;
						}
						for(x = 0; x < NDIRECT+1; x++){
							currDirectory = (struct dirent *)(map + (blockAddr*BSIZE) + x*(sizeof(struct dirent)));
							if(currDirectory->inum != 0){
								if(strcmp(currDirectory->name, ".") == 0 || strcmp(currDirectory->name, "..") == 0) {
									checkDirFormat++;
									if(isRoot == 1 && currDirectory->inum == 1)
										checkRootDir++;
								}					
							}
						}
						// Check 4: valid directory format
						if(checkDirFormat != 2){
							fprintf(stderr, "ERROR: directory not properly formatted.\n");
							exit(1);
						}
        				// Check 3: valid root inode
						if (isRoot == 1) {
							if(checkRootDir != 2){	
								fprintf(stderr, "ERROR: root directory does not exist.\n");
								exit(1);
							}
						}
					}
		        }

		        if (dip->size > BSIZE * NDIRECT) {
		            int *indirect = (int *)(map + (blockAddr*BSIZE));
		            for (k = 0; k < BSIZE/4; k++) {
		                int block = *(indirect + k);
						// Check if address is valid
		                if (block != 0) {
			            	// Check 2: bad address in inode
			                if (block < ((int)BBLOCK(sb->nblocks, sb->ninodes))+1) {
			                    fprintf(stderr, "ERROR: bad address in inode.\n");
			                    exit(1);
			                }
							// Check 8: check used blocks are only used once
			                if (usedBlocks[block] == 1) {
			                    fprintf(stderr, "ERROR: address used more than once.\n");
			                    exit(1);
			                }
		                    usedBlocks[block] = 1;
							// Check 6: check used blocks in bitmap
				            int bitmapLocation = (*((char*)bitmap + (block >> 3)) >> (block & 7)) & 1;
			                if (bitmapLocation == 0) {
			                    fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
			                    exit(1);
			                }
		                }
		            }
		        }

			}
			dip++;
		} else {
        	// Check 1: valid type
			fprintf(stderr,"ERROR: bad inode.\n");
			exit(1);
		}
    }

    numLinks[1]++;
	
    // Check 7: check used blocks are marked bitmap and are used
    int block;
    for(block = 0; block < sb->size; block++) {
        int bitmapLocation = (*((char*)bitmap + (block >> 3)) >> (block & 7)) & 1;
        if (bitmapLocation == 1) {
            if (usedBlocks[block] == 0) {
                fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.\n");
                exit(1);
            }
        }
    }

	dip = (struct dinode *)(map + 2*BSIZE);
    for (i = 0; i < sb->ninodes; i++) {
		type = dip->type;
        if (type != 0) {
            inodeUsed[i] = 1;
        }
		// Check 9: check used inodes reference a directory
        if (inodeUsed[i] == 1 && usedinodeDirectory[i] == 0) {
            fprintf(stderr, "ERROR: inode marked use but not found in a directory.\n");
            exit(1);
        }
		// Check 10: check used inodes in directory are in inode table
        if (usedinodeDirectory[i] == 1 && inodeUsed[i] == 0) {
            fprintf(stderr, "ERROR: inode referred to in directory but marked free.\n");
            exit(1);
        }
		// Check 12: No extra links allowed for directories
        if (type == 1 && numLinks[i] != 1) {
            fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
            exit(1);
        }
		// Check 11: hard links to file match files reference count
        if (numLinks[i] != dip->nlink) {
            fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
            exit(1);
        }
		dip++;
    }
    if (munmap(map, fileInfo.st_size) == -1) {
      close(fd);
      printf("pzip: error unmapping file\n");
      exit(1);
    }
    exit(0);
}
}
}
