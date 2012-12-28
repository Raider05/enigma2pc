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
 
#include "libdha.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "kernelhelper/dhahelper.h"

#ifdef __unix__
#include <unistd.h>
#endif

#if 0
#if defined(__SUNPRO_C) || defined(sun) || defined(__sun)
#include <sys/psw.h>
#else
#include <sys/seg.h>
#endif
#include <sys/v86.h>
#endif
 
#if defined(Lynx) && defined(__powerpc__)
/* let's mimick the Linux Alpha stuff for LynxOS so we don't have
 * to change too much code
 */
#include <smem.h>
 
static unsigned char *pciConfBase;
 
static __inline__ unsigned long
static swapl(unsigned long val)
{
	unsigned char *p = (unsigned char *)&val;
	return ((p[3] << 24) | (p[2] << 16) | (p[1] << 8) | (p[0] << 0));
}
 
 
#define BUS(tag) (((tag)>>16)&0xff)
#define DFN(tag) (((tag)>>8)&0xff)
 
#define PCIBIOS_DEVICE_NOT_FOUND	0x86
#define PCIBIOS_SUCCESSFUL		0x00
 
int pciconfig_read(
          unsigned char bus,
          unsigned char dev,
          unsigned char offset,
          int len,		/* unused, alway 4 */
          unsigned long *val)
{
	unsigned long _val;
	unsigned long *ptr;
 
	dev >>= 3;
	if (bus || dev >= 16) {
		*val = 0xFFFFFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else {
		ptr = (unsigned long *)(pciConfBase + ((1<<dev) | offset));
		_val = swapl(*ptr);
	}
	*val = _val;
	return PCIBIOS_SUCCESSFUL;
}
 
int pciconfig_write(
          unsigned char bus,
          unsigned char dev,
          unsigned char offset,
          int len,		/* unused, alway 4 */
          unsigned long val)
{
	unsigned long _val;
	unsigned long *ptr;
 
	dev >>= 3;
	_val = swapl(val);
	if (bus || dev >= 16) {
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else {
		ptr = (unsigned long *)(pciConfBase + ((1<<dev) | offset));
		*ptr = _val;
	}
	return PCIBIOS_SUCCESSFUL;
}
#endif
 
#if !defined(__powerpc__)
struct pci_config_reg {
    /* start of official PCI config space header */
    union {
        unsigned long device_vendor;
	struct {
	    unsigned short vendor;
	    unsigned short device;
	} dv;
    } dv_id;
#define _device_vendor dv_id.device_vendor
#define _vendor dv_id.dv.vendor
#define _device dv_id.dv.device
    union {
        unsigned long status_command;
	struct {
	    unsigned short command;
	    unsigned short status;
	} sc;
    } stat_cmd;
#define _status_command stat_cmd.status_command
#define _command stat_cmd.sc.command
#define _status  stat_cmd.sc.status
    union {
        unsigned long class_revision;
	struct {
	    unsigned char rev_id;
	    unsigned char prog_if;
	    unsigned char sub_class;
	    unsigned char base_class;
	} cr;
    } class_rev;
#define _class_revision class_rev.class_revision
#define _rev_id     class_rev.cr.rev_id
#define _prog_if    class_rev.cr.prog_if
#define _sub_class  class_rev.cr.sub_class
#define _base_class class_rev.cr.base_class
    union {
        unsigned long bist_header_latency_cache;
	struct {
	    unsigned char cache_line_size;
	    unsigned char latency_timer;
	    unsigned char header_type;
	    unsigned char bist;
	} bhlc;
    } bhlc;
#define _bist_header_latency_cache bhlc.bist_header_latency_cache
#define _cache_line_size bhlc.bhlc.cache_line_size
#define _latency_timer   bhlc.bhlc.latency_timer
#define _header_type     bhlc.bhlc.header_type
#define _bist            bhlc.bhlc.bist
    union {
	struct {
	    unsigned long dv_base0;
	    unsigned long dv_base1;
	    unsigned long dv_base2;
	    unsigned long dv_base3;
	    unsigned long dv_base4;
	    unsigned long dv_base5;
	} dv;
	struct {
	    unsigned long bg_rsrvd[2];
	    unsigned char primary_bus_number;
	    unsigned char secondary_bus_number;
	    unsigned char subordinate_bus_number;
	    unsigned char secondary_latency_timer;
	    unsigned char io_base;
	    unsigned char io_limit;
	    unsigned short secondary_status;
	    unsigned short mem_base;
	    unsigned short mem_limit;
	    unsigned short prefetch_mem_base;
	    unsigned short prefetch_mem_limit;
	} bg;
    } bc;
#define	_base0				bc.dv.dv_base0
#define	_base1				bc.dv.dv_base1
#define	_base2				bc.dv.dv_base2
#define	_base3				bc.dv.dv_base3
#define	_base4				bc.dv.dv_base4
#define	_base5				bc.dv.dv_base5
#define	_primary_bus_number		bc.bg.primary_bus_number
#define	_secondary_bus_number		bc.bg.secondary_bus_number
#define	_subordinate_bus_number		bc.bg.subordinate_bus_number
#define	_secondary_latency_timer	bc.bg.secondary_latency_timer
#define _io_base			bc.bg.io_base
#define _io_limit			bc.bg.io_limit
#define _secondary_status		bc.bg.secondary_status
#define _mem_base			bc.bg.mem_base
#define _mem_limit			bc.bg.mem_limit
#define _prefetch_mem_base		bc.bg.prefetch_mem_base
#define _prefetch_mem_limit		bc.bg.prefetch_mem_limit
    unsigned long rsvd1;
    unsigned long rsvd2;
    unsigned long _baserom;
    unsigned long rsvd3;
    unsigned long rsvd4;
    union {
        unsigned long max_min_ipin_iline;
	struct {
	    unsigned char int_line;
	    unsigned char int_pin;
	    unsigned char min_gnt;
	    unsigned char max_lat;
	} mmii;
    } mmii;
#define _max_min_ipin_iline mmii.max_min_ipin_iline
#define _int_line mmii.mmii.int_line
#define _int_pin  mmii.mmii.int_pin
#define _min_gnt  mmii.mmii.min_gnt
#define _max_lat  mmii.mmii.max_lat
    /* I don't know how accurate or standard this is (DHD) */
    union {
	unsigned long user_config;
	struct {
	    unsigned char user_config_0;
	    unsigned char user_config_1;
	    unsigned char user_config_2;
	    unsigned char user_config_3;
	} uc;
    } uc;
#define _user_config uc.user_config
#define _user_config_0 uc.uc.user_config_0
#define _user_config_1 uc.uc.user_config_1
#define _user_config_2 uc.uc.user_config_2
#define _user_config_3 uc.uc.user_config_3
    /* end of official PCI config space header */
    unsigned long _pcibusidx;
    unsigned long _pcinumbus;
    unsigned long _pcibuses[16];
    unsigned short _configtype;   /* config type found                   */
    unsigned short _ioaddr;       /* config type 1 - private I/O addr    */
    unsigned long _cardnum;       /* config type 2 - private card number */
};
#else
/* ppc is big endian, swapping bytes is not quite enough
 * to interpret the PCI config registers...
 */
struct pci_config_reg {
    /* start of official PCI config space header */
    union {
        unsigned long device_vendor;
	struct {
	    unsigned short device;
	    unsigned short vendor;
	} dv;
    } dv_id;
#define _device_vendor dv_id.device_vendor
#define _vendor dv_id.dv.vendor
#define _device dv_id.dv.device
    union {
        unsigned long status_command;
	struct {
	    unsigned short status;
	    unsigned short command;
	} sc;
    } stat_cmd;
#define _status_command stat_cmd.status_command
#define _command stat_cmd.sc.command
#define _status  stat_cmd.sc.status
    union {
        unsigned long class_revision;
	struct {
	    unsigned char base_class;
	    unsigned char sub_class;
	    unsigned char prog_if;
	    unsigned char rev_id;
	} cr;
    } class_rev;
#define _class_revision class_rev.class_revision
#define _rev_id     class_rev.cr.rev_id
#define _prog_if    class_rev.cr.prog_if
#define _sub_class  class_rev.cr.sub_class
#define _base_class class_rev.cr.base_class
    union {
        unsigned long bist_header_latency_cache;
	struct {
	    unsigned char bist;
	    unsigned char header_type;
	    unsigned char latency_timer;
	    unsigned char cache_line_size;
	} bhlc;
    } bhlc;
#define _bist_header_latency_cache bhlc.bist_header_latency_cache
#define _cache_line_size bhlc.bhlc.cache_line_size
#define _latency_timer   bhlc.bhlc.latency_timer
#define _header_type     bhlc.bhlc.header_type
#define _bist            bhlc.bhlc.bist
    union {
	struct {
	    unsigned long dv_base0;
	    unsigned long dv_base1;
	    unsigned long dv_base2;
	    unsigned long dv_base3;
	    unsigned long dv_base4;
	    unsigned long dv_base5;
	} dv;
/* ?? */
	struct {
	    unsigned long bg_rsrvd[2];
 
	    unsigned char secondary_latency_timer;
	    unsigned char subordinate_bus_number;
	    unsigned char secondary_bus_number;
	    unsigned char primary_bus_number;
 
	    unsigned short secondary_status;
	    unsigned char io_limit;
	    unsigned char io_base;
 
	    unsigned short mem_limit;
	    unsigned short mem_base;
 
	    unsigned short prefetch_mem_limit;
	    unsigned short prefetch_mem_base;
	} bg;
    } bc;
#define	_base0				bc.dv.dv_base0
#define	_base1				bc.dv.dv_base1
#define	_base2				bc.dv.dv_base2
#define	_base3				bc.dv.dv_base3
#define	_base4				bc.dv.dv_base4
#define	_base5				bc.dv.dv_base5
#define	_primary_bus_number		bc.bg.primary_bus_number
#define	_secondary_bus_number		bc.bg.secondary_bus_number
#define	_subordinate_bus_number		bc.bg.subordinate_bus_number
#define	_secondary_latency_timer	bc.bg.secondary_latency_timer
#define _io_base			bc.bg.io_base
#define _io_limit			bc.bg.io_limit
#define _secondary_status		bc.bg.secondary_status
#define _mem_base			bc.bg.mem_base
#define _mem_limit			bc.bg.mem_limit
#define _prefetch_mem_base		bc.bg.prefetch_mem_base
#define _prefetch_mem_limit		bc.bg.prefetch_mem_limit
    unsigned long rsvd1;
    unsigned long rsvd2;
    unsigned long _baserom;
    unsigned long rsvd3;
    unsigned long rsvd4;
    union {
        unsigned long max_min_ipin_iline;
	struct {
	    unsigned char max_lat;
	    unsigned char min_gnt;
	    unsigned char int_pin;
	    unsigned char int_line;
	} mmii;
    } mmii;
#define _max_min_ipin_iline mmii.max_min_ipin_iline
#define _int_line mmii.mmii.int_line
#define _int_pin  mmii.mmii.int_pin
#define _min_gnt  mmii.mmii.min_gnt
#define _max_lat  mmii.mmii.max_lat
    /* I don't know how accurate or standard this is (DHD) */
    union {
	unsigned long user_config;
	struct {
	    unsigned char user_config_3;
	    unsigned char user_config_2;
	    unsigned char user_config_1;
	    unsigned char user_config_0;
	} uc;
    } uc;
#define _user_config uc.user_config
#define _user_config_0 uc.uc.user_config_0
#define _user_config_1 uc.uc.user_config_1
#define _user_config_2 uc.uc.user_config_2
#define _user_config_3 uc.uc.user_config_3
    /* end of official PCI config space header */
    unsigned long _pcibusidx;
    unsigned long _pcinumbus;
    unsigned long _pcibuses[16];
    unsigned short _ioaddr;       /* config type 1 - private I/O addr    */
    unsigned short _configtype;   /* config type found                   */
    unsigned long _cardnum;       /* config type 2 - private card number */
};
#endif

#define MAX_DEV_PER_VENDOR_CFG1 64
#define MAX_PCI_DEVICES_PER_BUS 32
#define MAX_PCI_DEVICES         64
#define NF ((void (*)())NULL), { 0.0, 0, 0, NULL }
#define PCI_MULTIFUNC_DEV	0x80
#define PCI_ID_REG              0x00
#define PCI_CMD_STAT_REG        0x04
#define PCI_CLASS_REG           0x08
#define PCI_HEADER_MISC         0x0C
#define PCI_MAP_REG_START       0x10
#define PCI_MAP_ROM_REG         0x30
#define PCI_INTERRUPT_REG       0x3C
#define PCI_INTERRUPT_PIN	0x3D	/* 8 bits */
#define PCI_MIN_GNT		0x3E	/* 8 bits */
#define PCI_MAX_LAT		0x3F	/* 8 bits */
#define PCI_REG_USERCONFIG      0x40
 
static int pcibus=-1, pcicard=-1, pcifunc=-1 ;
/*static struct pci_device *pcidev=NULL ;*/
 
#if defined(__alpha__)
#define PCI_EN 0x00000000
#else
#define PCI_EN 0x80000000
#endif
 
#define	PCI_MODE1_ADDRESS_REG		0xCF8
#define	PCI_MODE1_DATA_REG		0xCFC
 
#define	PCI_MODE2_ENABLE_REG		0xCF8
#ifdef PC98
#define	PCI_MODE2_FORWARD_REG		0xCF9
#else
#define	PCI_MODE2_FORWARD_REG		0xCFA
#endif

/* cpu depended stuff */
#if defined(__alpha__)
#include "sysdep/pci_alpha.c"
#elif defined(__ia64__)
#include "sysdep/pci_ia64.c"
#elif defined(__sparc__)
#include "sysdep/pci_sparc.c"
#elif defined( __arm32__ )
#include "sysdep/pci_arm32.c"
#elif defined(__powerpc__)
#include "sysdep/pci_powerpc.c"
#elif defined( __i386__ )
#include "sysdep/pci_x86.c"
#else
#include "sysdep/pci_generic_cpu.c"
#endif

static int pcicards=0 ;
static pciinfo_t *pci_lst;
 
static void identify_card(struct pci_config_reg *pcr)
{
 
  if (pcicards>=MAX_PCI_DEVICES) return ;
 
  pci_lst[pcicards].bus     = pcibus ;
  pci_lst[pcicards].card    = pcicard ;
  pci_lst[pcicards].func    = pcifunc ;
  pci_lst[pcicards].vendor  = pcr->_vendor ;
  pci_lst[pcicards].device  = pcr->_device ;
  pci_lst[pcicards].base0   = 0xFFFFFFFF ;
  pci_lst[pcicards].base1   = 0xFFFFFFFF ;
  pci_lst[pcicards].base2   = 0xFFFFFFFF ;
  pci_lst[pcicards].base3   = 0xFFFFFFFF ;
  pci_lst[pcicards].base4   = 0xFFFFFFFF ;
  pci_lst[pcicards].base5   = 0xFFFFFFFF ;
  pci_lst[pcicards].baserom = 0x000C0000 ;
  if (pcr->_base0) pci_lst[pcicards].base0 = pcr->_base0 &
                     ((pcr->_base0&0x1) ? 0xFFFFFFFC : 0xFFFFFFF0) ;
  if (pcr->_base1) pci_lst[pcicards].base1 = pcr->_base1 &
                     ((pcr->_base1&0x1) ? 0xFFFFFFFC : 0xFFFFFFF0) ;
  if (pcr->_base2) pci_lst[pcicards].base2 = pcr->_base2 &
                     ((pcr->_base2&0x1) ? 0xFFFFFFFC : 0xFFFFFFF0) ;
  if (pcr->_base3) pci_lst[pcicards].base3 = pcr->_base3 &
                     ((pcr->_base0&0x1) ? 0xFFFFFFFC : 0xFFFFFFF0) ;
  if (pcr->_base4) pci_lst[pcicards].base4 = pcr->_base4 &
                     ((pcr->_base1&0x1) ? 0xFFFFFFFC : 0xFFFFFFF0) ;
  if (pcr->_base5) pci_lst[pcicards].base5 = pcr->_base5 &
                     ((pcr->_base2&0x1) ? 0xFFFFFFFC : 0xFFFFFFF0) ;
  if (pcr->_baserom) pci_lst[pcicards].baserom = pcr->_baserom ;
  pci_lst[pcicards].irq = pcr->_int_line;
  pci_lst[pcicards].ipin= pcr->_int_pin;
  pci_lst[pcicards].gnt = pcr->_min_gnt;
  pci_lst[pcicards].lat = pcr->_max_lat;
 
  pcicards++;
}

/*main(int argc, char *argv[])*/
static int __pci_scan(pciinfo_t *pci_list,unsigned *num_pci)
{
    unsigned int idx;
    struct pci_config_reg pcr;
    int do_mode1_scan = 0, do_mode2_scan = 0;
    int func, hostbridges=0;
    int ret = -1;
    
    pci_lst = pci_list;
    pcicards = 0;
 
    ret = enable_app_io();
    if (ret != 0)
	return(ret);

    if((pcr._configtype = pci_config_type()) == 0xFFFF) return ENODEV;
 
    /* Try pci config 1 probe first */
 
    if ((pcr._configtype == 1) || do_mode1_scan) {
    /*printf("\nPCI probing configuration type 1\n");*/
 
    pcr._ioaddr = 0xFFFF;
 
    pcr._pcibuses[0] = 0;
    pcr._pcinumbus = 1;
    pcr._pcibusidx = 0;
    idx = 0;
 
    do {
        /*printf("Probing for devices on PCI bus %d:\n\n", pcr._pcibusidx);*/
 
        for (pcr._cardnum = 0x0; pcr._cardnum < MAX_PCI_DEVICES_PER_BUS;
		pcr._cardnum += 0x1) {
	  func = 0;
	  do { /* loop over the different functions, if present */
	    pcr._device_vendor = pci_get_vendor(pcr._pcibuses[pcr._pcibusidx], pcr._cardnum,
						func);
            if ((pcr._vendor == 0xFFFF) || (pcr._device == 0xFFFF))
                break;   /* nothing there */
 
	    /*printf("\npci bus 0x%x cardnum 0x%02x function 0x%04x: vendor 0x%04x device 0x%04x\n",
	        pcr._pcibuses[pcr._pcibusidx], pcr._cardnum, func,
		pcr._vendor, pcr._device);*/
	    pcibus = pcr._pcibuses[pcr._pcibusidx];
	    pcicard = pcr._cardnum;
	    pcifunc = func;
 
	    pcr._status_command = pci_config_read_long(pcr._pcibuses[pcr._pcibusidx],
					pcr._cardnum,func,PCI_CMD_STAT_REG);
	    pcr._class_revision = pci_config_read_long(pcr._pcibuses[pcr._pcibusidx],
					pcr._cardnum,func,PCI_CLASS_REG);
	    pcr._bist_header_latency_cache = pci_config_read_long(pcr._pcibuses[pcr._pcibusidx],
					pcr._cardnum,func,PCI_HEADER_MISC);
	    pcr._base0 = pci_config_read_long(pcr._pcibuses[pcr._pcibusidx],
					pcr._cardnum,func,PCI_MAP_REG_START);
	    pcr._base1 = pci_config_read_long(pcr._pcibuses[pcr._pcibusidx],
					pcr._cardnum,func,PCI_MAP_REG_START+4);
	    pcr._base2 = pci_config_read_long(pcr._pcibuses[pcr._pcibusidx],
					pcr._cardnum,func,PCI_MAP_REG_START+8);
	    pcr._base3 = pci_config_read_long(pcr._pcibuses[pcr._pcibusidx],
					pcr._cardnum,func,PCI_MAP_REG_START+0x0C);
	    pcr._base4 = pci_config_read_long(pcr._pcibuses[pcr._pcibusidx],
					pcr._cardnum,func,PCI_MAP_REG_START+0x10);
	    pcr._base5 = pci_config_read_long(pcr._pcibuses[pcr._pcibusidx],
					pcr._cardnum,func,PCI_MAP_REG_START+0x14);
	    pcr._baserom = pci_config_read_long(pcr._pcibuses[pcr._pcibusidx],
					pcr._cardnum,func,PCI_MAP_ROM_REG);
#if 0
	    pcr._int_pin = pci_config_read_byte(pcr._pcibuses[pcr._pcibusidx],
					pcr._cardnum,func,PCI_INTERRUPT_PIN);
	    pcr._int_line = pci_config_read_byte(pcr._pcibuses[pcr._pcibusidx],
					pcr._cardnum,func,PCI_INTERRUPT_REG);
	    pcr._min_gnt = pci_config_read_byte(pcr._pcibuses[pcr._pcibusidx],
					pcr._cardnum,func,PCI_MIN_GNT);
	    pcr._max_lat = pci_config_read_byte(pcr._pcibuses[pcr._pcibusidx],
					pcr._cardnum,func,PCI_MAX_LAT);
#else
	    pcr._max_min_ipin_iline = pci_config_read_long(pcr._pcibuses[pcr._pcibusidx],
					pcr._cardnum,func,PCI_INTERRUPT_REG);
#endif
	    pcr._user_config = pci_config_read_long(pcr._pcibuses[pcr._pcibusidx],
					pcr._cardnum,func,PCI_REG_USERCONFIG);
            /* check for pci-pci bridges */
#define PCI_CLASS_MASK 		0xff000000
#define PCI_SUBCLASS_MASK 	0x00ff0000
#define PCI_CLASS_BRIDGE 	0x06000000
#define PCI_SUBCLASS_BRIDGE_PCI	0x00040000
	    switch(pcr._class_revision & (PCI_CLASS_MASK|PCI_SUBCLASS_MASK)) {
		case PCI_CLASS_BRIDGE|PCI_SUBCLASS_BRIDGE_PCI:
		    if (pcr._secondary_bus_number > 0) {
		        pcr._pcibuses[pcr._pcinumbus++] = pcr._secondary_bus_number;
		    }
			break;
		case PCI_CLASS_BRIDGE:
		    if ( ++hostbridges > 1) {
			pcr._pcibuses[pcr._pcinumbus] = pcr._pcinumbus;
			pcr._pcinumbus++;
		    }
			break;
		default:
			break;
	    }
	    if((func==0) && ((pcr._header_type & PCI_MULTIFUNC_DEV) == 0)) {
	        /* not a multi function device */
		func = 8;
	    } else {
	        func++;
	    }
 
	    if (idx++ >= MAX_PCI_DEVICES)
	        continue;
 
	    identify_card(&pcr);
	  } while( func < 8 );
        }
    } while (++pcr._pcibusidx < pcr._pcinumbus);
    }
 
#if !defined(__alpha__) && !defined(__powerpc__)
    /* Now try pci config 2 probe (deprecated) */
 
    if ((pcr._configtype == 2) || do_mode2_scan) {
    OUTPORT8(PCI_MODE2_ENABLE_REG, 0xF1);
    OUTPORT8(PCI_MODE2_FORWARD_REG, 0x00); /* bus 0 for now */
 
    /*printf("\nPCI probing configuration type 2\n");*/
 
    pcr._pcibuses[0] = 0;
    pcr._pcinumbus = 1;
    pcr._pcibusidx = 0;
    idx = 0;
 
    do {
        for (pcr._ioaddr = 0xC000; pcr._ioaddr < 0xD000; pcr._ioaddr += 0x0100){
	    OUTPORT8(PCI_MODE2_FORWARD_REG, pcr._pcibuses[pcr._pcibusidx]); /* bus 0 for now */
            pcr._device_vendor = INPORT32(pcr._ioaddr);
	    OUTPORT8(PCI_MODE2_FORWARD_REG, 0x00); /* bus 0 for now */
 
            if ((pcr._vendor == 0xFFFF) || (pcr._device == 0xFFFF))
                continue;
            if ((pcr._vendor == 0xF0F0) || (pcr._device == 0xF0F0))
                continue;  /* catch ASUS P55TP4XE motherboards */
 
	    /*printf("\npci bus 0x%x slot at 0x%04x, vendor 0x%04x device 0x%04x\n",
	        pcr._pcibuses[pcr._pcibusidx], pcr._ioaddr, pcr._vendor,
                pcr._device);*/
	    pcibus = pcr._pcibuses[pcr._pcibusidx] ;
	    pcicard = pcr._ioaddr ; pcifunc = 0 ;
 
	    OUTPORT8(PCI_MODE2_FORWARD_REG, pcr._pcibuses[pcr._pcibusidx]); /* bus 0 for now */
            pcr._status_command = INPORT32(pcr._ioaddr + 0x04);
            pcr._class_revision = INPORT32(pcr._ioaddr + 0x08);
            pcr._bist_header_latency_cache = INPORT32(pcr._ioaddr + 0x0C);
            pcr._base0 = INPORT32(pcr._ioaddr + 0x10);
            pcr._base1 = INPORT32(pcr._ioaddr + 0x14);
            pcr._base2 = INPORT32(pcr._ioaddr + 0x18);
            pcr._base3 = INPORT32(pcr._ioaddr + 0x1C);
            pcr._base4 = INPORT32(pcr._ioaddr + 0x20);
            pcr._base5 = INPORT32(pcr._ioaddr + 0x24);
            pcr._baserom = INPORT32(pcr._ioaddr + 0x30);
            pcr._max_min_ipin_iline = INPORT8(pcr._ioaddr + 0x3C);
            pcr._user_config = INPORT32(pcr._ioaddr + 0x40);
	    OUTPORT8(PCI_MODE2_FORWARD_REG, 0x00); /* bus 0 for now */
 
            /* check for pci-pci bridges (currently we only know Digital) */
            if ((pcr._vendor == 0x1011) && (pcr._device == 0x0001))
                if (pcr._secondary_bus_number > 0)
                    pcr._pcibuses[pcr._pcinumbus++] = pcr._secondary_bus_number;
 
	    if (idx++ >= MAX_PCI_DEVICES)
	        continue;
 
	    identify_card(&pcr);
	}
    } while (++pcr._pcibusidx < pcr._pcinumbus);
 
    OUTPORT8(PCI_MODE2_ENABLE_REG, 0x00);
    }
 
#endif /* __alpha__ */
 
    disable_app_io();
    *num_pci = pcicards;
 
    return 0 ;
 
}

#if !defined(ENOTSUP)
#if defined(EOPNOTSUPP)
#define ENOTSUP EOPNOTSUPP
#else
#warning "ENOTSUP nor EOPNOTSUPP defined!"
#endif
#endif

int pci_scan(pciinfo_t *pci_list,unsigned *num_pci)
{
  int libdha_fd;
  if ( (libdha_fd = open("/dev/dhahelper",O_RDWR)) < 0)
  {
	return __pci_scan(pci_list,num_pci);
  }
  else
  {
	dhahelper_pci_device_t pci_dev;
	unsigned idx;
	idx = 0;
	while(ioctl(libdha_fd, DHAHELPER_PCI_FIND, &pci_dev)==0)
	{
	    pci_list[idx].bus = pci_dev.bus;
	    pci_list[idx].card = pci_dev.card;
	    pci_list[idx].func = pci_dev.func;
	    pci_list[idx].vendor = pci_dev.vendor;
	    pci_list[idx].device = pci_dev.device;
	    pci_list[idx].base0 = pci_dev.base0?pci_dev.base0:0xFFFFFFFF;
	    pci_list[idx].base1 = pci_dev.base1?pci_dev.base1:0xFFFFFFFF;
	    pci_list[idx].base2 = pci_dev.base2?pci_dev.base2:0xFFFFFFFF;
	    pci_list[idx].baserom = pci_dev.baserom?pci_dev.baserom:0x000C0000;
	    pci_list[idx].base3 = pci_dev.base3?pci_dev.base3:0xFFFFFFFF;
	    pci_list[idx].base4 = pci_dev.base4?pci_dev.base4:0xFFFFFFFF;
	    pci_list[idx].base5 = pci_dev.base5?pci_dev.base5:0xFFFFFFFF;
	    pci_list[idx].irq = pci_dev.irq;
	    pci_list[idx].ipin = pci_dev.ipin;
	    pci_list[idx].gnt = pci_dev.gnt;
	    pci_list[idx].lat = pci_dev.lat;
	    idx++;
	}
	*num_pci=idx;
	close(libdha_fd);
  }
  return 0;
}

int pci_config_read(unsigned char bus, unsigned char dev, unsigned char func,
		    unsigned char cmd, int len, unsigned long *val)
{
    int ret;
    int dhahelper_fd;
    if ( (dhahelper_fd = open("/dev/dhahelper",O_RDWR)) > 0)
    {
	int retval;
	dhahelper_pci_config_t pcic;
	pcic.operation = PCI_OP_READ;
	pcic.bus = bus;
	pcic.dev = dev;
	pcic.func = func;
	pcic.cmd = cmd;
	pcic.size = len;
	retval = ioctl(dhahelper_fd, DHAHELPER_PCI_CONFIG, &pcic);
	close(dhahelper_fd);
	*val = pcic.ret;
	return retval;
    }
    ret = enable_app_io();
    if (ret != 0)
	return(ret);
    switch(len)
    {
	case 4:
	    ret = pci_config_read_long(bus, dev, func, cmd);
	    break;
	case 2:
	    ret = pci_config_read_word(bus, dev, func, cmd);
	    break;
	case 1:
	    ret = pci_config_read_byte(bus, dev, func, cmd);
	    break;
	default:
	    printf("libdha_pci: wrong length to read: %u\n",len);
    }
    disable_app_io();

    *val = ret;
    return(0);
}

int pci_config_write(unsigned char bus, unsigned char dev, unsigned char func,
		    unsigned char cmd, int len, unsigned long val)
{
    int ret;
    
    int dhahelper_fd;
    if ( (dhahelper_fd = open("/dev/dhahelper",O_RDWR)) > 0)
    {
	int retval;
	dhahelper_pci_config_t pcic;
	pcic.operation = PCI_OP_WRITE;
	pcic.bus = bus;
	pcic.dev = dev;
	pcic.func = func;
	pcic.cmd = cmd;
	pcic.size = len;
	pcic.ret = val;
	retval = ioctl(dhahelper_fd, DHAHELPER_PCI_CONFIG, &pcic);
	close(dhahelper_fd);
	return retval;
    }
    ret = enable_app_io();
    if (ret != 0)
	return ret;
    switch(len)
    {
	case 4:
	    pci_config_write_long(bus, dev, func, cmd, val);
	    break;
	case 2:
	    pci_config_write_word(bus, dev, func, cmd, val);
	    break;
	case 1:
	    pci_config_write_byte(bus, dev, func, cmd, val);
	    break;
	default:
	    printf("libdha_pci: wrong length to read: %u\n",len);
    }
    disable_app_io();

    return 0;
}
