/*
   This file is based on:
   $XFree86: xc/programs/Xserver/hw/xfree86/etc/scanpci.c,v 3.34.2.17 1998/11/10 11:55:40 dawes Exp $
   Modified for readability by Nick Kurshev
*/

static int pci_config_type( void ) { return 1; }
#ifdef linux
#include <fcntl.h>
#include <sys/io.h>
#include <linux/pci.h>
#include "../../bswap.h"
#endif

#ifdef linux
static int pci_get_vendor(
	  unsigned char bus,
	  unsigned char dev,
	  int func)
{
    int retval;
    char path[100];
    int fd;
    short vendor, device;
    sprintf(path,"/proc/bus/pci/%02d/%02x.0", bus, dev);
    fd = open(path,O_RDONLY|O_SYNC);
    if (fd == -1) {
	    retval=0xFFFF;
    }
    else if (pread(fd, &vendor, 2, PCI_VENDOR_ID) == 2 &&
             pread(fd, &device, 2, PCI_DEVICE_ID) == 2) {
	    vendor = bswap_16(vendor);
	    device = bswap_16(device);
	    retval = vendor + (device<<16); /*no worries about byte order, 
	    				      all ppc are bigendian*/
    } else {
	    retval = 0xFFFF;
    }   
    if (fd > 0) {
	    close(fd);
    }
    return retval;
}
#else
static int pci_get_vendor(
          unsigned char bus,
          unsigned char dev,
          int func)
{
    int retval;
    pciconfig_read(bus, dev<<3, PCI_ID_REG, 4, &retval);
    return retval;
}
#endif
#ifdef linux
static long pci_config_read_long(
	  unsigned char bus,
	  unsigned char dev,
	  int func, 
	  unsigned cmd)
{
    long retval;
    char path[100];
    int fd;
    sprintf(path,"/proc/bus/pci/%02d/%02x.0", bus, dev);
    fd = open(path,O_RDONLY|O_SYNC);
    if (fd == -1) {
	    retval=0;
    }
    else if (pread(fd, &retval, 4, cmd) == 4) {
	    retval = bswap_32(retval);
    } else {
	    retval = 0;
    }   
    if (fd > 0) {
	    close(fd);
    }
    return retval;
}

static long pci_config_read_word(
	  unsigned char bus,
	  unsigned char dev,
	  int func, 
	  unsigned cmd)
{
    long retval;
    char path[100];
    int fd;
    sprintf(path,"/proc/bus/pci/%02d/%02x.0", bus, dev);
    fd = open(path,O_RDONLY|O_SYNC);
    if (fd == -1) {
	    retval=0;
    }
    else if (pread(fd, &retval, 2, cmd) == 2) {
	    retval = bswap_16(retval);
    } else {
	    retval = 0;
    }   
    if (fd > 0) {
	    close(fd);
    }
    return retval;
}

static long pci_config_read_byte(
	  unsigned char bus,
	  unsigned char dev,
	  int func, 
	  unsigned cmd)
{
    long retval;
    char path[100];
    int fd;
    sprintf(path,"/proc/bus/pci/%02d/%02x.0", bus, dev);
    fd = open(path,O_RDONLY|O_SYNC);
    if (fd == -1) {
	    retval=0;
    }
    else if (pread(fd, &retval, 1, cmd) != 1) {
	    retval = 0;
    }   
    if (fd > 0) {
	    close(fd);
    }
    return retval;
}

static void pci_config_write_long(
	  unsigned char bus,
	  unsigned char dev,
	  int func, 
	  unsigned cmd,
	  long val)
{
    char path[100];
    int fd;
    val = bswap_32(val);
    sprintf(path,"/proc/bus/pci/%02d/%02x.0", bus, dev);
    fd = open(path,O_RDONLY|O_SYNC);
    if (fd > 0) {
	    pwrite(fd, &val, 4, cmd);
	    close(fd);
    }
}

static void pci_config_write_word(
	  unsigned char bus,
	  unsigned char dev,
	  int func, 
	  unsigned cmd,
	  long val)
{
    char path[100];
    int fd;
    val = bswap_16(val);
    sprintf(path,"/proc/bus/pci/%02d/%02x.0", bus, dev);
    fd = open(path,O_RDONLY|O_SYNC);
    if (fd > 0) {
	    pwrite(fd, &val, 2, cmd);
	    close(fd);
    }
}

static void pci_config_write_byte(
	  unsigned char bus,
	  unsigned char dev,
	  int func, 
	  unsigned cmd,
	  long val)
{
    char path[100];
    int fd;
    sprintf(path,"/proc/bus/pci/%02d/%02x.0", bus, dev);
    fd = open(path,O_RDONLY|O_SYNC);
    if (fd > 0) {
	    pwrite(fd, &val, 1, cmd);
	    close(fd);
    }
}
#else
static long pci_config_read_long(
          unsigned char bus,
          unsigned char dev,
          int func, 
          unsigned cmd)
{
    long retval;
    pciconfig_read(bus, dev<<3, cmd, 4, &retval);
    return retval;
}

static long pci_config_read_word(
          unsigned char bus,
          unsigned char dev,
          int func, 
          unsigned cmd)
{
    long retval;
    pciconfig_read(bus, dev<<3, cmd, 2, &retval);
    return retval;
}

static long pci_config_read_byte(
          unsigned char bus,
          unsigned char dev,
          int func, 
          unsigned cmd)
{
    long retval;
    pciconfig_read(bus, dev<<3, cmd, 1, &retval);
    return retval;
}

static void pci_config_write_long(
          unsigned char bus,
          unsigned char dev,
          int func, 
          unsigned cmd,
	  long val)
{
    long retval;
    pciconfig_write(bus, dev<<3, cmd, 4, val);
    return retval;
}

static void pci_config_write_word(
          unsigned char bus,
          unsigned char dev,
          int func, 
          unsigned cmd,
	  long val)
{
    long retval;
    pciconfig_write(bus, dev<<3, cmd, 2, val);
    return retval;
}

static void pci_config_write_byte(
          unsigned char bus,
          unsigned char dev,
          int func, 
          unsigned cmd,
	  long val)
{
    long retval;
    pciconfig_write(bus, dev<<3, cmd, 1, val);
    return retval;
}
#endif
