/*
    Generic stuff to compile VIDIX only on any system (SCRATCH)
*/

#ifndef __ASM_MACROS_GENERIC_H
#define __ASM_MACROS_GENERIC_H

#warning This stuff is not ported on your system

static __inline__ void outb(short port,char val)
{
    printf("outb: generic function call\n");
    return;
}

static __inline__ void outw(short port,short val)
{
    printf("outw: generic function call\n");
    return;
}

static __inline__ void outl(short port,unsigned int val)
{
    printf("outl: generic function call\n");
    return;
}

static __inline__ unsigned int inb(short port)
{
    printf("inb: generic function call\n");
    return 0;
}

static __inline__ unsigned int inw(short port)
{
    printf("inw: generic function call\n");
    return 0;
}

static __inline__ unsigned int inl(short port)
{
    printf("inl: generic function call\n");
    return 0;
}

static __inline__ void intr_disable()
{
    printf("intr_disable: generic function call\n");
}

static __inline__ void intr_enable()
{
    printf("intr_enable: generic function call\n");
}

#endif
