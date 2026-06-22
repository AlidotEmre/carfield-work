CC     = gcc
CFLAGS = -Wall -I../driver

all: ioctl_test cluster_test paging_math_test

ioctl_test: ioctl_test.c
	$(CC) $(CFLAGS) -o $@ $<

cluster_test: cluster_test.c
	$(CC) $(CFLAGS) -o $@ $<

paging_math_test: paging_math_test.c ../driver/carfield_paging_math.c
	$(CC) $(CFLAGS) -o $@ paging_math_test.c ../driver/carfield_paging_math.c

clean:
	rm -f ioctl_test cluster_test paging_math_test
