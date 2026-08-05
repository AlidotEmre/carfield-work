#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include "../driver/alsaqr.h"

int main(void)
{
	int fd;
	struct alsaqr_ping ping;

	fd = open("/dev/alsaqr", O_RDWR);
	if (fd < 0) {
		perror("open /dev/alsaqr");
		return 1;
	}

	ping.value = 0xDEADBEEF;
	ping.echo  = 0;

	if (ioctl(fd, ALSAQR_PING, &ping) < 0) {
		perror("ioctl ALSAQR_PING");
		close(fd);
		return 1;
	}

	printf("Sent : 0x%08X\n", ping.value);
	printf("Echoed: 0x%08X\n", ping.echo);

	if (ping.value == ping.echo)
		printf("PASS: driver-userspace channel is working.\n");
	else
		printf("FAIL: values do not match!\n");

	close(fd);
	return 0;
}
