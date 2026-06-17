CC     = gcc
CFLAGS = -Wall -I../driver

all: ioctl_test

ioctl_test: ioctl_test.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f ioctl_test
