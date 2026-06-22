obj-m := carfield.o
carfield-objs := carfield.o carfield_paging.o carfield_paging_math.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build

all: modules
modules clean:
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) $@
