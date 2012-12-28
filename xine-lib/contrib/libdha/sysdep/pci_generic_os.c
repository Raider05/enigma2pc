/*
    Generic stuff to compile VIDIX only on any system (SCRATCH)
*/
#warn This stuff is not ported on yur system
static __inline__ int enable_os_io(void)
{
    printf("enable_os_io: generic function call\n");
    return 0;
}

static __inline__ int disable_os_io(void)
{
    printf("disable_os_io: generic function call\n");
    return 0;
}
