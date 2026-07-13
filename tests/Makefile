CC     = gcc
CFLAGS = -Wall -I../driver

all: ioctl_test cluster_test paging_math_test paging_ioctl_test mock_ot_test mbox_reg_test

ioctl_test: ioctl_test.c
	$(CC) $(CFLAGS) -o $@ $<

cluster_test: cluster_test.c
	$(CC) $(CFLAGS) -o $@ $<

paging_math_test: paging_math_test.c ../driver/carfield_paging_math.c
	$(CC) $(CFLAGS) -o $@ paging_math_test.c ../driver/carfield_paging_math.c

paging_ioctl_test: paging_ioctl_test.c
	$(CC) $(CFLAGS) -o $@ $<

mock_ot_test: mock_ot_test.c
	$(CC) $(CFLAGS) -o $@ $<

mbox_reg_test: mbox_reg_test.c ../driver/carfield_mbox_hw.h
	$(CC) $(CFLAGS) -o $@ mbox_reg_test.c

clean:
	rm -f ioctl_test cluster_test paging_math_test paging_ioctl_test mock_ot_test mbox_reg_test
