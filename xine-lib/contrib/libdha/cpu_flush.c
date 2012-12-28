/* CPU flush support */
#include <stdio.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include "libdha.h"
#include "kernelhelper/dhahelper.h"

void	cpu_flush(void *va,unsigned long length)
{
  int retval;
  int libdha_fd=-1;
  if( libdha_fd == -1) libdha_fd = open("/dev/dhahelper",O_RDWR);
  if (libdha_fd > 0)
  {
	dhahelper_cpu_flush_t _l2;
	_l2.va = va;
	_l2.length = length;
	retval = ioctl(libdha_fd, DHAHELPER_CPU_FLUSH, &_l2);
	close(libdha_fd);
  }
}
