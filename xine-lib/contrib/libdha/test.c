#include "libdha.h"
#include "pci_names.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h> /* for __WORDSIZE */

int main( void )
{
  pciinfo_t lst[MAX_PCI_DEVICES];
  unsigned i,num_pci;
  int err;
  err = pci_scan(lst,&num_pci);
  if(err)
  {
    printf("Error occured during pci scan: %s\n",strerror(err));
    return EXIT_FAILURE;
  }
  else
  {
    printf(" Bus:card:func vend:dev  base0   :base1   :base2   :baserom :irq:pin:gnt:lat\n");
    for(i=0;i<num_pci;i++)
#if __WORDSIZE > 32
	printf("%04X:%04X:%04X %04X:%04X %16X:%16X:%16X:%16X:%02X :%02X :%02X :%02X\n"
#else
	printf("%04X:%04X:%04X %04X:%04X %08X:%08X:%08X:%08X:%02X :%02X :%02X :%02X\n"
#endif
    	    ,lst[i].bus,lst[i].card,lst[i].func
	    ,lst[i].vendor,lst[i].device
	    ,lst[i].base0,lst[i].base1,lst[i].base2,lst[i].baserom
	    ,lst[i].irq,lst[i].ipin,lst[i].gnt,lst[i].lat);
    printf("Additional info:\n");
    printf("================\n");
    printf("base3   :base4   :base5   :name (vendor)\n");
    for(i=0;i<num_pci;i++)
    {
	const char *vname,*dname;
	dname = pci_device_name(lst[i].vendor,lst[i].device);
	dname = dname ? dname : "Unknown chip";
	vname = pci_vendor_name(lst[i].vendor);
	vname = vname ? vname : "Unknown chip";
	printf("%08X:%08X:%08X:%s (%s)\n"
		,lst[i].base3,lst[i].base4,lst[i].base5
		,dname,vname);
    }
  }
  return EXIT_SUCCESS;
}
