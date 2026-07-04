obj-m := carfield-mod.o
carfield-mod-y := carfield.o carfield_paging.o carfield_paging_math.o carfield_mock_ot.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) clean
