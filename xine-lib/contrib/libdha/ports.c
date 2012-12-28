/*
   (C) 2002 - library implementation by Nick Kyrshev
   XFree86 3.3.3 scanpci.c, modified for GATOS/win/gfxdump by Øyvind Aabling.
 */
/* $XConsortium: scanpci.c /main/25 1996/10/27 11:48:40 kaleb $ */
/*
 *  name:             scanpci.c
 *
 *  purpose:          This program will scan for and print details of
 *                    devices on the PCI bus.
 
 *  author:           Robin Cutshaw (robin@xfree86.org)
 *
 *  supported O/S's:  SVR4, UnixWare, SCO, Solaris,
 *                    FreeBSD, NetBSD, 386BSD, BSDI BSD/386,
 *                    Linux, Mach/386, ISC
 *                    DOS (WATCOM 9.5 compiler)
 *
 *  compiling:        [g]cc scanpci.c -o scanpci
 *                    for SVR4 (not Solaris), UnixWare use:
 *                        [g]cc -DSVR4 scanpci.c -o scanpci
 *                    for DOS, watcom 9.5:
 *                        wcc386p -zq -omaxet -7 -4s -s -w3 -d2 name.c
 *                        and link with PharLap or other dos extender for exe
 *
 */
 
/* $XFree86: xc/programs/Xserver/hw/xfree86/etc/scanpci.c,v 3.34.2.17 1998/11/10 11:55:40 dawes Exp $ */
 
/*
 * Copyright 1995 by Robin Cutshaw <robin@XFree86.Org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the names of the above listed copyright holder(s)
 * not be used in advertising or publicity pertaining to distribution of
 * the software without specific, written prior permission.  The above listed
 * copyright holder(s) make(s) no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * THE ABOVE LISTED COPYRIGHT HOLDER(S) DISCLAIM(S) ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL THE ABOVE LISTED COPYRIGHT HOLDER(S) BE
 * LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY
 * DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef ARCH_ALPHA
#include <sys/io.h>
#endif
#include <unistd.h>

#include "libdha.h"
#include "AsmMacros.h"
#include "kernelhelper/dhahelper.h"

/* OS depended stuff */
#if defined (linux)
#include "sysdep/pci_linux.c"
#elif defined (__FreeBSD_kernel__)
#include "sysdep/pci_freebsd.c"
#elif defined (__386BSD__)
#include "sysdep/pci_386bsd.c"
#elif defined (__NetBSD__)
#include "sysdep/pci_netbsd.c"
#elif defined (__OpenBSD__)
#include "sysdep/pci_openbsd.c"
#elif defined (__bsdi__)
#include "sysdep/pci_bsdi.c"
#elif defined (Lynx)
#include "sysdep/pci_lynx.c"
#elif defined (MACH386)
#include "sysdep/pci_mach386.c"
#elif defined (__SVR4)
#if !defined(SVR4)
#define SVR4
#endif
#include "sysdep/pci_svr4.c"
#elif defined (SCO)
#include "sysdep/pci_sco.c"
#elif defined (ISC)
#include "sysdep/pci_isc.c"
#elif defined (__EMX__)
#include "sysdep/pci_os2.c"
#elif defined (_WIN32) || defined(__CYGWIN__)
#include "sysdep/pci_win32.c"
#else
#include "sysdep/pci_generic_os.c"
#endif

static int dhahelper_fd=-1;
static unsigned dhahelper_counter=0;
int enable_app_io( void )
{
    if((dhahelper_fd=open("/dev/dhahelper",O_RDWR)) < 0) return enable_os_io();
    dhahelper_counter++;
    return 0;
}

int disable_app_io( void )
{
  dhahelper_counter--;
  if(dhahelper_fd > 0)
  {
    if(!dhahelper_counter)
    {
	close(dhahelper_fd);
	dhahelper_fd = -1;
    }
  }
  else return disable_os_io();
  return 0;
}

unsigned char  INPORT8(unsigned idx)
{
    if (dhahelper_fd > 0)
    {
	dhahelper_port_t _port;
	
	_port.operation = PORT_OP_READ;
	_port.addr = idx;
	_port.size = 1;
        if (ioctl(dhahelper_fd, DHAHELPER_PORT, &_port) == 0)
	    return _port.value;
    }
    return inb(idx);
}

unsigned short INPORT16(unsigned idx)
{
    if (dhahelper_fd > 0)
    {
	dhahelper_port_t _port;
	
	_port.operation = PORT_OP_READ;
	_port.addr = idx;
	_port.size = 2;
        if (ioctl(dhahelper_fd, DHAHELPER_PORT, &_port) == 0)
	    return _port.value;
    }
    return inw(idx);
}

unsigned       INPORT32(unsigned idx)
{
    if (dhahelper_fd > 0)
    {
	dhahelper_port_t _port;
	
	_port.operation = PORT_OP_READ;
	_port.addr = idx;
	_port.size = 4;
        if (ioctl(dhahelper_fd, DHAHELPER_PORT, &_port) == 0)
	    return _port.value;
    }
    return inl(idx);
}

void          OUTPORT8(unsigned idx,unsigned char val)
{
    if (dhahelper_fd > 0)
    {
	dhahelper_port_t _port;
	
	_port.operation = PORT_OP_WRITE;
	_port.addr = idx;
	_port.size = 1;
	_port.value = val;
        if (ioctl(dhahelper_fd, DHAHELPER_PORT, &_port) == 0)
	    return;
    }
    else outb(idx,val);
}

void          OUTPORT16(unsigned idx,unsigned short val)
{
    if (dhahelper_fd > 0)
    {
	dhahelper_port_t _port;
	
	_port.operation = PORT_OP_WRITE;
	_port.addr = idx;
	_port.size = 2;
	_port.value = val;
        if (ioctl(dhahelper_fd, DHAHELPER_PORT, &_port) == 0)
	    return;
    }
    else outw(idx,val);
}

void          OUTPORT32(unsigned idx,unsigned val)
{
    if (dhahelper_fd > 0)
    {
	dhahelper_port_t _port;
	
	_port.operation = PORT_OP_WRITE;
	_port.addr = idx;
	_port.size = 4;
	_port.value = val;
        if (ioctl(dhahelper_fd, DHAHELPER_PORT, &_port) == 0)
	    return;
    }
    else outl(idx,val);
}

