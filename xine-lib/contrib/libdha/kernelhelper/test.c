#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>

#include "dhahelper.h"

int main(int argc, char *argv[])
{
    int fd;
    int ret;

    fd = open("/dev/dhahelper", O_RDWR);
    if(fd < 0){
	perror("dev/dhahelper");
	exit(1);
    }

    ioctl(fd, DHAHELPER_GET_VERSION, &ret);

    printf("api version: %d\n", ret);
    if (ret != API_VERSION)
	printf("incompatible api!\n");

    {
	void *mem;
	unsigned long size=256;
	mem = mmap(0,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
	printf("allocated to %p\n", mem); 

	if (argc > 1)
	    if (mem != 0)
	    {
 		int i;
 
		for (i = 0; i < 256; i++)
		    printf("[%x] ", *(int *)(mem+i));
		printf("\n");
	    }

	munmap((void *)mem, size);
    }

    return(0);
}
