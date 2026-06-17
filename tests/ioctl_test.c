#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include "../driver/carfield.h"

int main(void)
{
	int fd;
	struct carfield_ping ping;

	fd = open("/dev/carfield", O_RDWR);
	if (fd < 0) {
		perror("open /dev/carfield");
		return 1;
	}

	ping.value = 0xDEADBEEF;
	ping.echo  = 0;

	if (ioctl(fd, CARFIELD_PING, &ping) < 0) {
		perror("ioctl CARFIELD_PING");
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
