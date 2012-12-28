/* HW IRQ support */
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h> /* mlock */
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include "libdha.h"
#include "kernelhelper/dhahelper.h"


static int libdha_fd=-1;
static int hwirq_locks=0;

int	hwirq_install(int bus, int dev, int func,
		      int ar, u_long ao, uint32_t ad)
{
  int retval;
  if( libdha_fd == -1) libdha_fd = open("/dev/dhahelper",O_RDWR);
  hwirq_locks++;
  if (libdha_fd > 0)
  {
	dhahelper_irq_t _irq;
	_irq.bus = bus;
	_irq.dev = dev;
	_irq.func = func;
	_irq.ack_region = ar;
	_irq.ack_offset = ao;
	_irq.ack_data = ad;
	retval = ioctl(libdha_fd, DHAHELPER_INSTALL_IRQ, &_irq);
	return retval;
  }
  return errno;
}

int	hwirq_wait(unsigned irqnum)
{
  int retval;
  if (libdha_fd > 0)
  {
	dhahelper_irq_t _irq;
	_irq.num = irqnum;
	retval = ioctl(libdha_fd, DHAHELPER_ACK_IRQ, &_irq);
	return retval;
  }
  return EINVAL;
}

int	hwirq_uninstall(int bus, int dev, int func)
{
  if (libdha_fd > 0)
  {
	dhahelper_irq_t _irq;
	_irq.bus = bus;
	_irq.dev = dev;
	_irq.func = func;
	ioctl(libdha_fd, DHAHELPER_FREE_IRQ, &_irq);
  }
  if(!hwirq_locks) { close(libdha_fd); libdha_fd=-1; }
  return 0;
}
