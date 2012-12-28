/*
    libgha.h - Library for direct hardware access
    Copyrights:
    1996/10/27	- Robin Cutshaw (robin@xfree86.org)
		  XFree86 3.3.3 implementation
    1999	- Øyvind Aabling.
    		  Modified for GATOS/win/gfxdump.
    2002	- library implementation by Nick Kurshev
    
    supported O/S's:	SVR4, UnixWare, SCO, Solaris,
			FreeBSD, NetBSD, 386BSD, BSDI BSD/386,
			Linux, Mach/386, ISC
			DOS (WATCOM 9.5 compiler), Win9x (with mapdev.vxd)
    Licence: GPL
*/
#ifndef LIBDHA_H
#define LIBDHA_H

#if defined (__FreeBSD__)
# include <inttypes.h>
#else
# include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_DEV_PER_VENDOR_CFG1 64
#define MAX_PCI_DEVICES_PER_BUS 32
#define MAX_PCI_DEVICES         64
#define PCI_MULTIFUNC_DEV	0x80

typedef struct pciinfo_s
{
  int		bus,card,func;			/* PCI/AGP bus:card:func */
  unsigned short vendor,device;			/* Card vendor+device ID */
  unsigned long base0,base1,base2,baserom;	/* Memory and I/O base addresses */
  unsigned long base3,base4,base5;		/* Memory and I/O base addresses */
  unsigned char irq,ipin,gnt,lat;		/* assigned IRQ parameters for this card */
//  unsigned	base0_limit, base1_limit, base2_limit, baserom_limit;
}pciinfo_t;

extern int pci_config_read(unsigned char bus, unsigned char dev, unsigned char func,
			unsigned char cmd, int len, unsigned long *val);
extern int pci_config_write(unsigned char bus, unsigned char dev, unsigned char func,
			unsigned char cmd, int len, unsigned long val);
			/* Fill array pci_list which must have size MAX_PCI_DEVICES
			   and return 0 if sucessful */
extern int  pci_scan(pciinfo_t *pci_list,unsigned *num_card);

	    /* Enables/disables accessing to IO space from application side.
	       Should return 0 if o'k or errno on error. */
extern int enable_app_io( void );
extern int disable_app_io( void );

extern unsigned char  INPORT8(unsigned idx);
extern unsigned short INPORT16(unsigned idx);
extern unsigned       INPORT32(unsigned idx);
#define INPORT(idx) INPORT32(idx)
extern void          OUTPORT8(unsigned idx,unsigned char val);
extern void          OUTPORT16(unsigned idx,unsigned short val);
extern void          OUTPORT32(unsigned idx,unsigned val);
#define OUTPORT(idx,val) OUTPORT32(idx,val)

extern void *  map_phys_mem(unsigned long base, unsigned long size);
extern void    unmap_phys_mem(void *ptr, unsigned long size);

/*  These are the region types  */
#define MTRR_TYPE_UNCACHABLE 0
#define MTRR_TYPE_WRCOMB     1
#define MTRR_TYPE_WRTHROUGH  4
#define MTRR_TYPE_WRPROT     5
#define MTRR_TYPE_WRBACK     6
extern int	mtrr_set_type(unsigned base,unsigned size,int type);

/* Busmastering support */
		/* returns 0 if support exists else errno */
extern int	bm_open( void );
extern void	bm_close( void );
		/* Converts virtual memory addresses into physical
		   returns 0 if OK else - errno
		   parray should have enough length to accept length/page_size
		   elements. virt_addr can be located in non-continious memory
		   block and can be allocated by malloc(). (kmalloc() is not
		   needed). Note:  if you have some very old card which requires
		   continous memory block then you need to implement  bm_kmalloc
		   bm_kfree functions here. NOTE2: to be sure that every page of
		   region is present in physical memory (is not swapped out) use
		   m(un)lock functions. Note3: Probably your card will want to
		   have page-aligned block for DMA transfer so use
		   memalign(PAGE_SIZE,mem_size) function to alloc such memory. */
extern int	bm_virt_to_phys( void * virt_addr, unsigned long length,
			    unsigned long * parray );
		/* Converts virtual memory addresses into bus address
		   Works in the same way as bm_virt_to_phys.
		   WARNING: This function will be die after implementing
		   bm_alloc_pci_shmem() because we really can't pass
		   any memory address to card. Example: 64-bit linear address
		   can't be passed into 32-bit card. Even more - some old
		   cards can access 24-bit address space only */
extern int	bm_virt_to_bus( void * virt_addr, unsigned long length,
			    unsigned long * barray );

		/* NOTE: bm_alloc_pci_shmem() and bm_free_pci_shmem()
			 are still not implemented!
			 arguments:
			 pciinfo_t - specifies pci card for which memory should be shared
			 bitness   - can be 16,24,32,64 specifies addressing possibilities
				     of the card
			 length    - specifies size of memory which should allocated
			 op        - specifies direction as combination flags TO_CARD,FROM_CARD
			 Return value - should be tuned
			 we need to have something like this:
			 struct pci_shmem
			 {
			    void * handler;
			    void * virt_addr
			    void * array_of_bus_addr[];
			    unsigned long length;
			 }
		   NOTE2: After finalizing of these functions bm_virt_to_bus() will be die */
extern void *	bm_alloc_pci_shmem(pciinfo_t *, unsigned mem_bitness, unsigned long length,int op );
extern void	bm_free_pci_shmem(void * pci_shmem);

extern int	bm_lock_mem( const void * addr, unsigned long length );
extern int	bm_unlock_mem( const void * addr, unsigned long length );

/* HWIRQ support */

extern int	hwirq_install(int bus, int dev, int func,
			      int areg, unsigned long aoff, uint32_t adata);
extern int	hwirq_wait(unsigned irqnum);
extern int	hwirq_uninstall(int bus, int dev, int func);

/* CPU flushing support */
extern void	cpu_flush(void *va,unsigned long length);

extern void     libdha_exit(const char *message, int level);

#ifdef __cplusplus
}
#endif

#endif
