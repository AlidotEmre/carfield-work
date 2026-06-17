CC     = gcc
CFLAGS = -Wall -I../driver

all: ioctl_test cluster_test

ioctl_test: ioctl_test.c
	$(CC) $(CFLAGS) -o $@ $<

cluster_test: cluster_test.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f ioctl_test cluster_test
