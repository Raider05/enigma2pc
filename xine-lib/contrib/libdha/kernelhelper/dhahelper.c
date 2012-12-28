/*
    Direct Hardware Access kernel helper
    
    (C) 2002 Alex Beregszaszi <alex@naxine.org>
    (C) 2002-2003 Nick Kurshev <nickols_k@mail.ru>
    (C) 2002-2004 Måns Rullgård <mru@users.sourceforge.net>

    Accessing hardware from userspace as USER (no root needed!)

    Tested on 2.2.x (2.2.19), 2.4.x (2.4.3,2.4.17) and 2.6.1.
    
    License: GPL
    
    WARNING! THIS MODULE VIOLATES SEVERAL SECURITY LINES! DON'T USE IT
    ON PRODUCTION SYSTEMS, ONLY AT HOME, ON A "SINGLE-USER" SYSTEM.
    NO WARRANTY!
    
    IF YOU WANT TO USE IT ON PRODUCTION SYSTEMS THEN PLEASE READ 'README'
    FILE TO KNOW HOW TO PREVENT ANONYMOUS ACCESS TO THIS MODULE.

    Tech:
	Communication between userspace and kernelspace goes over character
	device using ioctl.

    Usage:
	mknod -m 600 /dev/dhahelper c 252 0
	
	Also you can change the major number, setting the "dhahelper_major"
	module parameter, the default is 252, specified in dhahelper.h.
	
	Note: do not use other than minor==0, the module forbids it.

    TODO:
	* select (request?) a "valid" major number (from Linux project? ;)
	* make security
	* is pci handling needed? (libdha does this with lowlevel port funcs)
	* is mttr handling needed?
	* test on older kernels (2.0.x (?))
*/

#ifndef MODULE
#define MODULE
#endif

#ifndef __KERNEL__
#define __KERNEL__
#endif

#include <linux/config.h>

#ifdef CONFIG_MODVERSION
#define MODVERSION
#include <linux/modversions.h>
#endif

#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
#include <linux/malloc.h>
#else
#include <linux/slab.h>
#endif

#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>

#include <linux/mman.h>

#include <linux/fs.h>
#include <linux/unistd.h>

#ifdef CONFIG_MTRR 
#include <asm/mtrr.h>
#endif
#ifdef CONFIG_DEVFS_FS
#include <linux/devfs_fs_kernel.h>
#endif

#include "dhahelper.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define pte_offset(p,a) pte_offset_kernel(p,a)
#define LockPage(p) SetPageLocked(p)
#define UnlockPage(p) ClearPageLocked(p)
#define irqreturn(n) return(n)
#else
#define irqreturn_t void
#define irqreturn(n) return
#endif

MODULE_AUTHOR("Alex Beregszaszi <alex@naxine.org>, Nick Kurshev <nickols_k@mail.ru>, Måns Rullgård <mru@users.sf.net>");
MODULE_DESCRIPTION("Provides userspace access to hardware");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

static int dhahelper_major = DEFAULT_MAJOR;
MODULE_PARM(dhahelper_major, "i");
MODULE_PARM_DESC(dhahelper_major, "Major number of dhahelper characterdevice");

/* 0 = silent */
/* 1 = report errors (default) */
/* 2 = debug */
static int dhahelper_verbosity = 1;
MODULE_PARM(dhahelper_verbosity, "i");
MODULE_PARM_DESC(dhahelper_verbosity, "Level of verbosity (0 = silent, 1 = only errors, 2 = debug)");

static int dhahelper_open(struct inode *inode, struct file *file)
{
    if (dhahelper_verbosity > 1)
	printk(KERN_DEBUG "dhahelper: device opened\n");

    if (MINOR(inode->i_rdev) != 0)
	return -ENXIO;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
    MOD_INC_USE_COUNT;
#endif

    return 0;
}

static int dhahelper_release(struct inode *inode, struct file *file)
{
    if (dhahelper_verbosity > 1)
	printk(KERN_DEBUG "dhahelper: device released\n");

    if (MINOR(inode->i_rdev) != 0)
	return -ENXIO;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
    MOD_DEC_USE_COUNT;
#endif

    return 0;
}

static int dhahelper_get_version(int * arg)
{
	int version = API_VERSION;

	if (copy_to_user(arg, &version, sizeof(int)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy to userspace\n");
		return -EFAULT;
	}
	return 0;
}

static int dhahelper_port(dhahelper_port_t * arg)
{
	dhahelper_port_t port;
	if (copy_from_user(&port, arg, sizeof(dhahelper_port_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy from userspace\n");
		return -EFAULT;
	}
	switch(port.operation)
	{
		case PORT_OP_READ:
		{
		    switch(port.size)
		    {
			case 1:
			    port.value = inb(port.addr);
			    break;
			case 2:
			    port.value = inw(port.addr);
			    break;
			case 4:
			    port.value = inl(port.addr);
			    break;
			default:
			    if (dhahelper_verbosity > 0)
				printk(KERN_ERR "dhahelper: invalid port read size (%d)\n",
				    port.size);
			    return -EINVAL;
		    }
		    break;
		}
		case PORT_OP_WRITE:
		{
		    switch(port.size)
		    {
			case 1:
			    outb(port.value, port.addr);
			    break;
			case 2:
			    outw(port.value, port.addr);
			    break;
			case 4:
			    outl(port.value, port.addr);
			    break;
			default:
			    if (dhahelper_verbosity > 0)
				printk(KERN_ERR "dhahelper: invalid port write size (%d)\n",
				    port.size);
			    return -EINVAL;
		    }
		    break;
		}
		default:
		    if (dhahelper_verbosity > 0)
		        printk(KERN_ERR "dhahelper: invalid port operation (%d)\n",
		    	    port.operation);
		    return -EINVAL;
	}
	/* copy back only if read was performed */
	if (port.operation == PORT_OP_READ)
	if (copy_to_user(arg, &port, sizeof(dhahelper_port_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy to userspace\n");
		return -EFAULT;
	}
	return 0;
}

/*******************************/
/* Memory management functions */
/* from kernel:/drivers/media/video/bttv-driver.c */
/*******************************/

#define MDEBUG(x)	do { } while(0)		/* Debug memory management */

/* [DaveM] I've recoded most of this so that:
 * 1) It's easier to tell what is happening
 * 2) It's more portable, especially for translating things
 *    out of vmalloc mapped areas in the kernel.
 * 3) Less unnecessary translations happen.
 *
 * The code used to assume that the kernel vmalloc mappings
 * existed in the page tables of every process, this is simply
 * not guarenteed.  We now use pgd_offset_k which is the
 * defined way to get at the kernel page tables.
 */

/* Given PGD from the address space's page table, return the kernel
 * virtual mapping of the physical memory mapped at ADR.
 */
static inline unsigned long uvirt_to_kva(pgd_t *pgd, unsigned long adr)
{
        unsigned long ret = 0UL;
	pmd_t *pmd;
	pte_t *ptep, pte;
  
	if (!pgd_none(*pgd)) {
                pmd = pmd_offset(pgd, adr);
                if (!pmd_none(*pmd)) {
                        ptep = pte_offset(pmd, adr);
                        pte = *ptep;
                        if(pte_present(pte)) {
				ret  = (unsigned long) page_address(pte_page(pte));
				ret |= (adr & (PAGE_SIZE - 1));
				
			}
                }
        }
        MDEBUG(printk("uv2kva(%lx-->%lx)", adr, ret));
	return ret;
}

static inline unsigned long uvirt_to_bus(unsigned long adr) 
{
        unsigned long kva, ret;

        kva = uvirt_to_kva(pgd_offset(current->mm, adr), adr);
	ret = virt_to_bus((void *)kva);
        MDEBUG(printk("uv2b(%lx-->%lx)", adr, ret));
        return ret;
}

static inline unsigned long uvirt_to_pa(unsigned long adr) 
{
        unsigned long kva, ret;

        kva = uvirt_to_kva(pgd_offset(current->mm, adr), adr);
	ret = virt_to_phys((void *)kva);
        MDEBUG(printk("uv2b(%lx-->%lx)", adr, ret));
        return ret;
}

static inline unsigned long kvirt_to_bus(unsigned long va) 
{
        unsigned long kva, ret;

        kva = uvirt_to_kva(pgd_offset_k(va), va);
	ret = virt_to_bus((void *)kva);
        MDEBUG(printk("kv2b(%lx-->%lx)", adr, ret));
        return ret;
}

/* Here we want the physical address of the memory.
 * This is used when initializing the contents of the
 * area and marking the pages as reserved.
 */
static inline unsigned long kvirt_to_pa(unsigned long va) 
{
        unsigned long kva, ret;

        kva = uvirt_to_kva(pgd_offset_k(va), va);
	ret = __pa(kva);
        MDEBUG(printk("kv2pa(%lx-->%lx)", adr, ret));
        return ret;
}

static void * rvmalloc(signed long size)
{
	void * mem;
	unsigned long adr, page;

	mem=vmalloc_32(size);
	if (mem) 
	{
		memset(mem, 0, size); /* Clear the ram out, no junk to the user */
	        adr=(unsigned long) mem;
		while (size > 0) 
                {
	                page = kvirt_to_pa(adr);
			SetPageReserved(virt_to_page(__va(page)));
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
	}
	return mem;
}

static int pag_lock(unsigned long addr)
{
	unsigned long page;
	unsigned long kva;

	kva = uvirt_to_kva(pgd_offset(current->mm, addr), addr);
	if(kva)
	{
	    lock_it:
	    page = uvirt_to_pa((unsigned long)addr);
	    LockPage(virt_to_page(__va(page)));
	    SetPageReserved(virt_to_page(__va(page)));
	}
	else
	{
	    copy_from_user(&page,(char *)addr,1); /* try access it */
	    kva = uvirt_to_kva(pgd_offset(current->mm, addr), addr);
	    if(kva) goto lock_it;
	    else return EPERM;
	}
	return 0;
}

static int pag_unlock(unsigned long addr)
{
	    unsigned long page;
	    unsigned long kva;

	    kva = uvirt_to_kva(pgd_offset(current->mm, addr), addr);
	    if(kva)
	    {
		page = uvirt_to_pa((unsigned long)addr);
		UnlockPage(virt_to_page(__va(page)));
		ClearPageReserved(virt_to_page(__va(page)));
		return 0;
	    }
	    return EPERM;
}


static void rvfree(void * mem, signed long size)
{
        unsigned long adr, page;
        
	if (mem) 
	{
	        adr=(unsigned long) mem;
		while (size > 0) 
                {
	                page = kvirt_to_pa(adr);
			ClearPageReserved(virt_to_page(__va(page)));
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
		vfree(mem);
	}
}


static int dhahelper_virt_to_phys(dhahelper_vmi_t *arg)
{
	dhahelper_vmi_t mem;
	unsigned long i,nitems;
	char *addr;
	if (copy_from_user(&mem, arg, sizeof(dhahelper_vmi_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy from userspace\n");
		return -EFAULT;
	}
	nitems = mem.length / PAGE_SIZE;
	if(mem.length % PAGE_SIZE) nitems++;
	addr = mem.virtaddr;
	for(i=0;i<nitems;i++)
	{
	    unsigned long result;
	    result = uvirt_to_pa((unsigned long)addr);
	    if (copy_to_user(&mem.realaddr[i], &result, sizeof(unsigned long)))
	    {
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy to userspace\n");
		return -EFAULT;
	    }
	    addr += PAGE_SIZE;
	}
	return 0;
}

static int dhahelper_virt_to_bus(dhahelper_vmi_t *arg)
{
	dhahelper_vmi_t mem;
	unsigned long i,nitems;
	char *addr;
	if (copy_from_user(&mem, arg, sizeof(dhahelper_vmi_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy from userspace\n");
		return -EFAULT;
	}
	nitems = mem.length / PAGE_SIZE;
	if(mem.length % PAGE_SIZE) nitems++;
	addr = mem.virtaddr;
	for(i=0;i<nitems;i++)
	{
	    unsigned long result;
	    result = uvirt_to_bus((unsigned long)addr);
	    if (copy_to_user(&mem.realaddr[i], &result, sizeof(unsigned long)))
	    {
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy to userspace\n");
		return -EFAULT;
	    }
	    addr += PAGE_SIZE;
	}
	return 0;
}


static int dhahelper_alloc_pa(dhahelper_mem_t *arg)
{
	dhahelper_mem_t mem;
	if (copy_from_user(&mem, arg, sizeof(dhahelper_mem_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy from userspace\n");
		return -EFAULT;
	}
	mem.addr = rvmalloc(mem.length);
	if (copy_to_user(arg, &mem, sizeof(dhahelper_mem_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy to userspace\n");
		return -EFAULT;
	}
	return 0;
}

static int dhahelper_free_pa(dhahelper_mem_t *arg)
{
	dhahelper_mem_t mem;
	if (copy_from_user(&mem, arg, sizeof(dhahelper_mem_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy from userspace\n");
		return -EFAULT;
	}
	rvfree(mem.addr,mem.length);
	return 0;
}

static int dhahelper_lock_mem(dhahelper_mem_t *arg)
{
	dhahelper_mem_t mem;
	int retval;
	unsigned long i,nitems,addr;
	if (copy_from_user(&mem, arg, sizeof(dhahelper_mem_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy from userspace\n");
		return -EFAULT;
	}
	nitems = mem.length / PAGE_SIZE;
	if(mem.length % PAGE_SIZE) nitems++;
	addr = (unsigned long)mem.addr;
	for(i=0;i<nitems;i++)
	{
	    retval = pag_lock((unsigned long)addr);
	    if(retval)
	    {
		unsigned long j;
		addr = (unsigned long)mem.addr;
		for(j=0;j<i;j++)
		{
		    pag_unlock(addr);
		    addr +=  PAGE_SIZE;
		}
		return retval;
	    }
	    addr += PAGE_SIZE;
	}
	return 0;
}

static int dhahelper_unlock_mem(dhahelper_mem_t *arg)
{
	dhahelper_mem_t mem;
	int retval;
	unsigned long i,nitems,addr;
	if (copy_from_user(&mem, arg, sizeof(dhahelper_mem_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy from userspace\n");
		return -EFAULT;
	}
	nitems = mem.length / PAGE_SIZE;
	if(mem.length % PAGE_SIZE) nitems++;
	addr = (unsigned long)mem.addr;
	for(i=0;i<nitems;i++)
	{
	    retval = pag_unlock((unsigned long)addr);
	    if(retval) return retval;
	    addr += PAGE_SIZE;
	}
	return 0;
}

static struct dha_irq {
    spinlock_t lock;
    unsigned long flags;
    int handled;
    int rcvd;
    volatile u32 *ack_addr;
    u32 ack_data;
    struct pci_dev *dev;
    wait_queue_head_t wait;
    unsigned long count;
} dha_irqs[256];

static irqreturn_t dhahelper_irq_handler(int irq, void *dev_id,
					 struct pt_regs *regs)
{
    spin_lock_irqsave(&dha_irqs[irq].lock, dha_irqs[irq].flags);
    if(dha_irqs[irq].handled){
	dha_irqs[irq].rcvd = 1;
	dha_irqs[irq].count++;
	if(dha_irqs[irq].ack_addr){
	    *dha_irqs[irq].ack_addr = dha_irqs[irq].ack_data;
	    mb();
	}
	wake_up_interruptible(&dha_irqs[irq].wait);
    }
    spin_unlock_irqrestore(&dha_irqs[irq].lock, dha_irqs[irq].flags);
    irqreturn(0);
}

static int dhahelper_install_irq(dhahelper_irq_t *arg)
{
    dhahelper_irq_t my_irq;
    struct pci_dev *pci;
    long rlen;
    int retval;
    long ack_addr;
    int irqn;

    if (copy_from_user(&my_irq, arg, sizeof(dhahelper_irq_t)))
    {
	if (dhahelper_verbosity > 0)
	    printk(KERN_ERR "dhahelper: failed copy from userspace\n");
	return -EFAULT;
    }

    if(!(pci = pci_find_slot(my_irq.bus, PCI_DEVFN(my_irq.dev, my_irq.func))))
	return -EINVAL;

    rlen = pci_resource_len(pci, my_irq.ack_region);
    if(my_irq.ack_offset > rlen - 4)
	return -EINVAL;

    irqn = pci->irq;

    spin_lock_irqsave(&dha_irqs[irqn].lock,
		      dha_irqs[irqn].flags);

    if(dha_irqs[irqn].handled){
	retval = -EBUSY;
	goto fail;
    }

    if(my_irq.ack_region >= 0){
	ack_addr = pci_resource_start(pci, my_irq.ack_region);
	ack_addr += my_irq.ack_offset;
#ifdef CONFIG_ALPHA
	ack_addr += ((struct pci_controller *) pci->sysdata)->dense_mem_base;
#endif
	/* FIXME:  Other architectures */

	dha_irqs[irqn].ack_addr = phys_to_virt(ack_addr);
	dha_irqs[irqn].ack_data = my_irq.ack_data;
    } else {
	dha_irqs[irqn].ack_addr = 0;
    }

    dha_irqs[irqn].lock = SPIN_LOCK_UNLOCKED;
    dha_irqs[irqn].flags = 0;
    dha_irqs[irqn].rcvd = 0;
    dha_irqs[irqn].dev = pci;
    init_waitqueue_head(&dha_irqs[irqn].wait);
    dha_irqs[irqn].count = 0;

    retval = request_irq(irqn, dhahelper_irq_handler,
			 SA_SHIRQ, "dhahelper", pci);

    if(retval < 0)
	goto fail;

    copy_to_user(&arg->num, &irqn, sizeof(irqn));

    dha_irqs[irqn].handled = 1;

out:
    spin_unlock_irqrestore(&dha_irqs[irqn].lock,
			   dha_irqs[irqn].flags);
    return retval;

fail:
    if(retval == -EINVAL){
	printk("dhahelper: bad irq number or handler\n");
    } else if(retval == -EBUSY){
	printk("dhahelper: IRQ %u busy\n", irqn);
    } else {
	printk("dhahelper: Could not install irq handler...\n");
    }
    printk("dhahelper: Perhaps you need to let your BIOS assign an IRQ to your video card\n");
    goto out;
}

static int dhahelper_free_irq(dhahelper_irq_t *arg)
{
	dhahelper_irq_t irq;
	struct pci_dev *pci;
	int irqn;

	if (copy_from_user(&irq, arg, sizeof(dhahelper_irq_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy from userspace\n");
		return -EFAULT;
	}

	pci = pci_find_slot(irq.bus, PCI_DEVFN(irq.dev, irq.func));
	if(!pci)
	    return -EINVAL;

	irqn = pci->irq;

	spin_lock_irqsave(&dha_irqs[irqn].lock, dha_irqs[irqn].flags);
	if(dha_irqs[irqn].handled) {
		free_irq(irqn, pci);
		dha_irqs[irqn].handled = 0;
		printk("IRQ %i: %li\n", irqn, dha_irqs[irqn].count);
	}
	spin_unlock_irqrestore(&dha_irqs[irqn].lock, dha_irqs[irqn].flags);
	return 0;
}

static int dhahelper_ack_irq(dhahelper_irq_t *arg)
{
	dhahelper_irq_t irq;
	int retval = 0;
	DECLARE_WAITQUEUE(wait, current);
	if (copy_from_user(&irq, arg, sizeof(dhahelper_irq_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy from userspace\n");
		return -EFAULT;
	}
	if(irq.num > 255) return -EINVAL;
	if(!dha_irqs[irq.num].handled) return -ESRCH;
	add_wait_queue(&dha_irqs[irq.num].wait, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	for(;;){
		int r;
		spin_lock_irqsave(&dha_irqs[irq.num].lock,
				  dha_irqs[irq.num].flags);
		r = dha_irqs[irq.num].rcvd;
		spin_unlock_irqrestore(&dha_irqs[irq.num].lock,
				       dha_irqs[irq.num].flags);

		if(r){
			dha_irqs[irq.num].rcvd = 0;
			break;
		}

		if(signal_pending(current)){
			retval = -ERESTARTSYS;
			break;
		}

		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dha_irqs[irq.num].wait, &wait);	
	return retval;
}

static int dhahelper_cpu_flush(dhahelper_cpu_flush_t *arg)
{
	dhahelper_cpu_flush_t my_l2;
	if (copy_from_user(&my_l2, arg, sizeof(dhahelper_cpu_flush_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy from userspace\n");
		return -EFAULT;
	}
#if defined(__i386__)
	/* WBINVD writes all modified cache lines back to main memory */
	if(boot_cpu_data.x86 > 3) { __asm __volatile("wbinvd":::"memory"); }
#else
	/* FIXME!!!*/
	mb(); /* declared in "asm/system.h" */
#endif
	return 0;
}

static struct pci_dev *pdev = NULL;
static int dhahelper_pci_find(dhahelper_pci_device_t *arg)
{
	dhahelper_pci_device_t this_dev;
	pdev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, pdev);
	if(pdev)
	{
	    this_dev.bus = pdev->bus->number;
	    this_dev.card = PCI_SLOT(pdev->devfn);
	    this_dev.func = PCI_FUNC(pdev->devfn);
	    this_dev.vendor = pdev->vendor;
	    this_dev.device = pdev->device;
	    this_dev.base0 = pci_resource_start (pdev, 0);
	    this_dev.base1 = pci_resource_start (pdev, 1);
	    this_dev.base2 = pci_resource_start (pdev, 2);
	    pci_read_config_dword(pdev, pdev->rom_base_reg, (u32*)&this_dev.baserom);
	    this_dev.base3 = pci_resource_start (pdev, 3);
	    this_dev.base4 = pci_resource_start (pdev, 4);
	    this_dev.base5 = pci_resource_start (pdev, 5);
	    pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, &this_dev.irq);
	    pci_read_config_byte(pdev, PCI_INTERRUPT_PIN, &this_dev.ipin);
	    pci_read_config_byte(pdev, PCI_MIN_GNT, &this_dev.gnt);
	    pci_read_config_byte(pdev, PCI_MAX_LAT, &this_dev.lat);
	}
	else memset(&this_dev,0,sizeof(dhahelper_pci_device_t));
	if (copy_to_user(arg, &this_dev, sizeof(dhahelper_pci_device_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy to userspace\n");
		return -EFAULT;
	}
	return pdev?0:-ENODATA;
}

static int dhahelper_pci_config(dhahelper_pci_config_t *arg)
{
    dhahelper_pci_config_t op;
    struct pci_dev *pdev;
	if (copy_from_user(&op, arg, sizeof(dhahelper_pci_config_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy from userspace\n");
		return -EFAULT;
	}
	pdev = pci_find_slot(op.bus,PCI_DEVFN(op.dev,op.func));
	if(!pdev)
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: can't identify device\n");
		return -EFAULT;
	}
	switch(op.operation)
	{
	    case PCI_OP_READ:
		switch(op.size)
		{
		    case 1:
			pci_read_config_byte(pdev,op.cmd,(u8*)&op.ret);
			break;
		    case 2:
			pci_read_config_word(pdev,op.cmd,(u16*)&op.ret);
			break;
		    case 4:
			pci_read_config_dword(pdev,op.cmd,(u32*)&op.ret);
			break;
		    default:
			if (dhahelper_verbosity > 0)
			    printk(KERN_ERR "dhahelper: wrong size of pci operation: %u \n",op.size);
			return -EFAULT;
		}
	    case PCI_OP_WRITE:
		switch(op.size)
		{
		    case 1:
			pci_write_config_byte(pdev,op.cmd,op.ret);
			break;
		    case 2:
			pci_write_config_word(pdev,op.cmd,op.ret);
			break;
		    case 4:
			pci_write_config_dword(pdev,op.cmd,op.ret);
			break;
		    default:
			if (dhahelper_verbosity > 0)
			    printk(KERN_ERR "dhahelper: wrong size of pci operation: %u \n",op.size);
			return -EFAULT;
		}
	    default:
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: unknown pci operation %i\n",op.operation);
		return -EFAULT;
	}
	if (copy_to_user(arg, &op, sizeof(dhahelper_pci_device_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy to userspace\n");
		return -EFAULT;
	}
	return 0;
}

static int dhahelper_mtrr(dhahelper_mtrr_t *arg)
{
#ifdef CONFIG_MTRR
    dhahelper_mtrr_t op;
	if (copy_from_user(&op, arg, sizeof(dhahelper_pci_config_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy from userspace\n");
		return -EFAULT;
	}
	switch(op.operation)
	{
	    case MTRR_OP_ADD:
		op.privat = mtrr_add (op.start,op.size,op.type,1);
		break;
	    case MTRR_OP_DEL:
		mtrr_del(op.privat, op.start, op.size);
                break;
	    default:
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: unknown mtrr operation %i\n",op.operation);
		return -EFAULT;
	}
	if (copy_to_user(arg, &op, sizeof(dhahelper_mtrr_t)))
	{
		if (dhahelper_verbosity > 0)
		    printk(KERN_ERR "dhahelper: failed copy to userspace\n");
		return -EFAULT;
	}
#endif
    return 0;
}

static int dhahelper_ioctl(struct inode *inode, struct file *file,
    unsigned int cmd, unsigned long arg)
{
    if (dhahelper_verbosity > 1)
	printk(KERN_DEBUG "dhahelper: ioctl(cmd=%x, arg=%lx)\n",
	    cmd, arg);

    if (MINOR(inode->i_rdev) != 0)
	return -ENXIO;

    switch(cmd)
    {
	case DHAHELPER_GET_VERSION: return dhahelper_get_version((int *)arg);
	case DHAHELPER_PORT:	    return dhahelper_port((dhahelper_port_t *)arg);
	case DHAHELPER_MTRR:	    return dhahelper_mtrr((dhahelper_mtrr_t *)arg);
	case DHAHELPER_PCI_CONFIG:  return dhahelper_pci_config((dhahelper_pci_config_t *)arg);
	case DHAHELPER_VIRT_TO_PHYS:return dhahelper_virt_to_phys((dhahelper_vmi_t *)arg);
	case DHAHELPER_VIRT_TO_BUS: return dhahelper_virt_to_bus((dhahelper_vmi_t *)arg);
	case DHAHELPER_ALLOC_PA:return dhahelper_alloc_pa((dhahelper_mem_t *)arg);
	case DHAHELPER_FREE_PA: return dhahelper_free_pa((dhahelper_mem_t *)arg);
	case DHAHELPER_LOCK_MEM: return dhahelper_lock_mem((dhahelper_mem_t *)arg);
	case DHAHELPER_UNLOCK_MEM: return dhahelper_unlock_mem((dhahelper_mem_t *)arg);
	case DHAHELPER_INSTALL_IRQ: return dhahelper_install_irq((dhahelper_irq_t *)arg);
	case DHAHELPER_ACK_IRQ: return dhahelper_ack_irq((dhahelper_irq_t *)arg);
	case DHAHELPER_FREE_IRQ: return dhahelper_free_irq((dhahelper_irq_t *)arg);
	case DHAHELPER_CPU_FLUSH: return dhahelper_cpu_flush((dhahelper_cpu_flush_t *)arg);
	case DHAHELPER_PCI_FIND: return dhahelper_pci_find((dhahelper_pci_device_t *)arg);
	default:
    	    if (dhahelper_verbosity > 0)
		printk(KERN_ERR "dhahelper: invalid ioctl (%x)\n", cmd);
	    return -EINVAL;
    }
    return 0;
}

/*
    fops functions were shamelessly stolen from linux-kernel project ;)
*/

static loff_t dhahelper_lseek(struct file * file, loff_t offset, int orig)
{
	switch (orig) {
		case 0:
			file->f_pos = offset;
			return file->f_pos;
		case 1:
			file->f_pos += offset;
			return file->f_pos;
		default:
			return -EINVAL;
	}
}

/*
 * This funcion reads the *physical* memory. The f_pos points directly to the 
 * memory location. 
 */
static ssize_t dhahelper_read(struct file * file, char * buf,
			size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	unsigned long end_mem;
	ssize_t read;
	
	end_mem = __pa(high_memory);
	if (p >= end_mem)
		return 0;
	if (count > end_mem - p)
		count = end_mem - p;
	read = 0;
#if defined(__sparc__) || defined(__mc68000__)
	/* we don't have page 0 mapped on sparc and m68k.. */
	if (p < PAGE_SIZE) {
		unsigned long sz = PAGE_SIZE-p;
		if (sz > count) 
			sz = count; 
		if (sz > 0) {
			if (clear_user(buf, sz))
				return -EFAULT;
			buf += sz; 
			p += sz; 
			count -= sz; 
			read += sz; 
		}
	}
#endif
	if (copy_to_user(buf, __va(p), count))
		return -EFAULT;
	read += count;
	*ppos += read;
	return read;
}

static ssize_t do_write_mem(struct file * file, void *p, unsigned long realp,
			    const char * buf, size_t count, loff_t *ppos)
{
	ssize_t written;

	written = 0;
#if defined(__sparc__) || defined(__mc68000__)
	/* we don't have page 0 mapped on sparc and m68k.. */
	if (realp < PAGE_SIZE) {
		unsigned long sz = PAGE_SIZE-realp;
		if (sz > count) sz = count; 
		/* Hmm. Do something? */
		buf+=sz;
		p+=sz;
		count-=sz;
		written+=sz;
	}
#endif
	if (copy_from_user(p, buf, count))
		return -EFAULT;
	written += count;
	*ppos += written;
	return written;
}

static ssize_t dhahelper_write(struct file * file, const char * buf, 
			 size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	unsigned long end_mem;

	end_mem = __pa(high_memory);
	if (p >= end_mem)
		return 0;
	if (count > end_mem - p)
		count = end_mem - p;
	return do_write_mem(file, __va(p), p, buf, count, ppos);
}

#ifndef pgprot_noncached

/*
 * This should probably be per-architecture in <asm/pgtable.h>
 */
static inline pgprot_t pgprot_noncached(pgprot_t _prot)
{
	unsigned long prot = pgprot_val(_prot);

#if defined(__i386__) || defined(__x86_64__)
	/* On PPro and successors, PCD alone doesn't always mean 
	    uncached because of interactions with the MTRRs. PCD | PWT
	    means definitely uncached. */ 
	if (boot_cpu_data.x86 > 3)
		prot |= _PAGE_PCD | _PAGE_PWT;
#elif defined(__powerpc__)
	prot |= _PAGE_NO_CACHE | _PAGE_GUARDED;
#elif defined(__mc68000__)
#ifdef SUN3_PAGE_NOCACHE
	if (MMU_IS_SUN3)
		prot |= SUN3_PAGE_NOCACHE;
	else
#endif
	if (MMU_IS_851 || MMU_IS_030)
		prot |= _PAGE_NOCACHE030;
	/* Use no-cache mode, serialized */
	else if (MMU_IS_040 || MMU_IS_060)
		prot = (prot & _CACHEMASK040) | _PAGE_NOCACHE_S;
#endif

	return __pgprot(prot);
}

#endif /* !pgprot_noncached */

/*
 * Architectures vary in how they handle caching for addresses 
 * outside of main memory.
 */
static inline int noncached_address(unsigned long addr)
{
#if defined(__i386__)
	/* 
	 * On the PPro and successors, the MTRRs are used to set
	 * memory types for physical addresses outside main memory, 
	 * so blindly setting PCD or PWT on those pages is wrong.
	 * For Pentiums and earlier, the surround logic should disable 
	 * caching for the high addresses through the KEN pin, but
	 * we maintain the tradition of paranoia in this code.
	 */
 	return !( test_bit(X86_FEATURE_MTRR, boot_cpu_data.x86_capability) ||
		  test_bit(X86_FEATURE_K6_MTRR, boot_cpu_data.x86_capability) ||
		  test_bit(X86_FEATURE_CYRIX_ARR, boot_cpu_data.x86_capability) ||
		  test_bit(X86_FEATURE_CENTAUR_MCR, boot_cpu_data.x86_capability) )
	  && addr >= __pa(high_memory);
#else
	return addr >= __pa(high_memory);
#endif
}

static int dhahelper_mmap(struct file * file, struct vm_area_struct * vma)
{
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	int err;

	/*
	 * Accessing memory above the top the kernel knows about or
	 * through a file pointer that was marked O_SYNC will be
	 * done non-cached.
	 */
	if (noncached_address(offset) || (file->f_flags & O_SYNC))
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	/* Don't try to swap out physical pages.. */
	vma->vm_flags |= VM_RESERVED;

	/*
	 * Don't dump addresses that are not real memory to a core file.
	 */
	if (offset >= __pa(high_memory) || (file->f_flags & O_SYNC))
		vma->vm_flags |= VM_IO;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
	err = remap_page_range(vma, vma->vm_start, offset,
			       vma->vm_end-vma->vm_start, vma->vm_page_prot);
#else
	err = remap_page_range(vma->vm_start, offset,
			       vma->vm_end-vma->vm_start, vma->vm_page_prot);
#endif
	if(err)
		return -EAGAIN;
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
static struct file_operations dhahelper_fops =
{
    /*llseek*/	dhahelper_lseek,
    /*read*/	dhahelper_read,
    /*write*/	dhahelper_write,
    /*readdir*/	NULL,
    /*poll*/	NULL,
    /*ioctl*/	dhahelper_ioctl,
    /*mmap*/	dhahelper_mmap,
    /*open*/	dhahelper_open,
    /*flush*/	NULL,
    /*release*/	dhahelper_release,
    /* zero out the last 5 entries too ? */
};
#else
static struct file_operations dhahelper_fops =
{
    owner:	THIS_MODULE,
    ioctl:	dhahelper_ioctl,
    open:	dhahelper_open,
    release:	dhahelper_release,
    llseek:	dhahelper_lseek,
    read:	dhahelper_read,
    write:	dhahelper_write,
    mmap:	dhahelper_mmap,
};
#endif

#ifdef CONFIG_DEVFS_FS
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
devfs_handle_t dha_devfsh;

static int
register_dev(void)
{
    dha_devfsh = devfs_register(NULL, "dhahelper", DEVFS_FL_NONE,
				dhahelper_major, 0,
				S_IFCHR | S_IRUSR | S_IWUSR,
				&dhahelper_fops, NULL);
    if(!dha_devfsh)
	return -EIO;
    return 0;
}

static void
unregister_dev(void)
{
    devfs_unregister(dha_devfsh);
}
#else /* VERSION < 2.6.0 */
static int
register_dev(void)
{
    devfs_mk_cdev(MKDEV(dhahelper_major, 0), S_IFCHR | S_IRUSR | S_IWUSR,
		  "dhahelper");
    if(register_chrdev(dhahelper_major, "dhahelper", &dhahelper_fops))
	return -EIO;
    return 0;
}

static void
unregister_dev(void)
{
    devfs_remove("dhahelper");
    unregister_chrdev(dhahelper_major, "dhahelper");
}
#endif /* VERSION < 2.6.0 */
#else
static int
register_dev(void)
{
    return register_chrdev(dhahelper_major, "dhahelper", &dhahelper_fops);
}

static void
unregister_dev(void)
{
    unregister_chrdev(dhahelper_major, "dhahelper");
}
#endif /* defined CONFIG_DEVFS_FS */


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
int init_module(void)
#else 
static int __init init_dhahelper(void)
#endif
{
    int err = 0;
    printk(KERN_INFO "Direct Hardware Access kernel helper (C) Alex Beregszaszi\n");

    err = register_dev();
    if(err){
    	if (dhahelper_verbosity > 0)
	    printk(KERN_ERR "dhahelper: unable to register character device (major: %d)\n",
		dhahelper_major);
	return err;
    }
    memset(dha_irqs, 0, sizeof(dha_irqs));
    return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
void cleanup_module(void)
#else
static void __exit exit_dhahelper(void)
#endif
{
    unsigned i;
    for(i=0;i<256;i++)
	if(dha_irqs[i].handled)
	    free_irq(i, dha_irqs[i].dev);

    unregister_dev();
}

#ifdef EXPORT_NO_SYMBOLS
EXPORT_NO_SYMBOLS;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
module_init(init_dhahelper);
module_exit(exit_dhahelper);
#endif
