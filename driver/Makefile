obj-m := alsaqr-mod.o
alsaqr-mod-y := alsaqr.o alsaqr_paging.o alsaqr_paging_math.o alsaqr_mock_ot.o alsaqr_mbox_hw.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) clean
