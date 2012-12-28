obj-m := dvbsoftwareca.o
dvbsoftwareca-objs := dvb_softwareca.o ca_netlink.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
clean:
	rm -f Module.symvers
	rm -f *.o
	rm -f *.ko
	rm -f *.mod.c
	rm -f .*.o.cmd
	rm -f .*.ko.cmd
	rm -f modules.order
	rm -Rf .tmp_versions
