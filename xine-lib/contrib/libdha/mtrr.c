/*
    mtrr.c - Stuff for optimizing memory access
    Copyrights:
    2002	- Linux version by Nick Kurshev
    Licence: GPL
*/

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "kernelhelper/dhahelper.h"
#include "libdha.h"

#if defined (__i386__) && defined (__NetBSD__)
#include <sys/param.h>
#if __NetBSD_Version__ > 105240000
#include <stdint.h>
#include <stdlib.h>
#include <machine/mtrr.h>
#include <machine/sysarch.h>
#endif
#endif

int	mtrr_set_type(unsigned base,unsigned size,int type)
{
    int dhahelper_fd;
    dhahelper_fd = open("/dev/dhahelper",O_RDWR);
    if(dhahelper_fd > 0)
    {
	int retval;
	dhahelper_mtrr_t mtrrs;
	mtrrs.operation = MTRR_OP_ADD;
	mtrrs.start = base;
	mtrrs.size = size;
	mtrrs.type = type;
	retval = ioctl(dhahelper_fd, DHAHELPER_ACK_IRQ, &mtrrs);
	close(dhahelper_fd);
	return retval;
    }
#if defined (__NetBSD__) && (__NetBSD_Version__) > 105240000
    {
    struct mtrr *mtrrp;
    int n;

    mtrrp = malloc(sizeof (struct mtrr));
    mtrrp->base = base;
    mtrrp->len = size;
    mtrrp->type = type;  
    mtrrp->flags = MTRR_VALID | MTRR_PRIVATE;
    n = 1;

    if (i386_set_mtrr(mtrrp, &n) < 0) {
	free(mtrrp);
	return errno;
    }
    free(mtrrp);
    return 0;
    }
#else
    {
    FILE * mtrr_fd;
    char * stype;
    switch(type)
    {
	case MTRR_TYPE_UNCACHABLE: stype = "uncachable"; break;
	case MTRR_TYPE_WRCOMB:	   stype = "write-combining"; break;
	case MTRR_TYPE_WRTHROUGH:  stype = "write-through"; break;
	case MTRR_TYPE_WRPROT:	   stype = "write-protect"; break;
	case MTRR_TYPE_WRBACK:	   stype = "write-back"; break;
	default:		   return EINVAL;
    }
    mtrr_fd = fopen("/proc/mtrr","wt");
    if(mtrr_fd)
    {
	char sout[256];
	unsigned wr_len;
	sprintf(sout,"base=0x%08X size=0x%08X type=%s\n",base,size,stype);
	wr_len = fprintf(mtrr_fd,"%s",sout);
	/*printf("MTRR: %s\n",sout);*/
	fclose(mtrr_fd);
	return wr_len == strlen(sout) ? 0 : EPERM;
    }
    }
#endif
    return ENOSYS;
}
