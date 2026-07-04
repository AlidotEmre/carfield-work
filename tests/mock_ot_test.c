/*
 * End-to-end userspace test for CARFIELD_MOCK_OT_XFORM (driver/carfield_mock_ot.c),
 * per MOCK_OT_SPEC.md §7. Requires the module loaded with mock_ot=1:
 *
 *   sudo insmod carfield-mod.ko mock_ot=1
 *   sudo ./mock_ot_test
 *
 * Fault-injection params (mock_delay_ms, mock_no_reply, mock_force_err,
 * mock_bad_xform, mock_corrupt_magic) are toggled at runtime through
 * /sys/module/carfield_mod/parameters/<name> -- no need to reload the
 * module between cases.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "../driver/carfield_mock_ot.h"

#define XOR_KEY   0x5A
#define PAGE_SZ   4096

static int set_param(const char *name, const char *value)
{
	char path[256];
	int fd, ret;

	snprintf(path, sizeof(path), "/sys/module/carfield_mod/parameters/%s", name);
	fd = open(path, O_WRONLY);
	if (fd < 0) {
		perror(path);
		return -1;
	}
	ret = write(fd, value, strlen(value));
	close(fd);
	return ret < 0 ? -1 : 0;
}

static void fill_pattern(uint8_t *buf, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		buf[i] = (uint8_t)(i * 167 + 7);
}

/*
 * Runs one transfer over buf[off, off+size), with everything else in
 * [0,total) acting as guard bytes. Verifies every payload byte equals
 * shadow^key and every guard byte is untouched.
 */
static int run_and_check(int fd, uint8_t *buf, size_t total,
			  size_t off, size_t size, uint8_t key,
			  const char *label)
{
	uint8_t *shadow;
	struct carfield_mock_ot_req req;
	size_t i;
	int ok = 1;

	shadow = malloc(total);
	if (!shadow) {
		perror("malloc shadow");
		return 0;
	}
	memcpy(shadow, buf, total);

	memset(&req, 0, sizeof(req));
	req.user_addr = (__u64)(unsigned long)(buf + off);
	req.user_size = size;

	if (ioctl(fd, CARFIELD_MOCK_OT_XFORM, &req) < 0) {
		printf("FAIL [%s]: ioctl error: %s (mock_status=%u)\n",
		       label, strerror(errno), req.mock_status);
		free(shadow);
		return 0;
	}

	for (i = 0; i < total; i++) {
		int in_payload = (i >= off && i < off + size);
		uint8_t expect = in_payload ? (uint8_t)(shadow[i] ^ key) : shadow[i];

		if (buf[i] != expect) {
			printf("FAIL [%s]: byte %zu (%s): got 0x%02x expected 0x%02x\n",
			       label, i, in_payload ? "payload" : "guard",
			       buf[i], expect);
			ok = 0;
		}
	}

	free(shadow);
	if (ok)
		printf("PASS [%s]: off=%zu size=%zu mock_status=%u\n",
		       label, off, size, req.mock_status);
	return ok;
}

static int case_single_page(int fd)
{
	uint8_t *buf = mmap(NULL, PAGE_SZ, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	int ok;

	if (buf == MAP_FAILED) {
		perror("mmap");
		return 0;
	}
	fill_pattern(buf, PAGE_SZ);
	ok = run_and_check(fd, buf, PAGE_SZ, 64, 200, XOR_KEY, "single-page");
	munmap(buf, PAGE_SZ);
	return ok;
}

static int case_straddle(int fd)
{
	size_t total = 4 * PAGE_SZ;
	uint8_t *buf = mmap(NULL, total, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	int ok;

	if (buf == MAP_FAILED) {
		perror("mmap");
		return 0;
	}
	fill_pattern(buf, total);
	ok = run_and_check(fd, buf, total, 0x123, 2 * PAGE_SZ + 100, XOR_KEY,
			    "page-straddling");
	munmap(buf, total);
	return ok;
}

static int case_large_malloc(int fd)
{
	size_t total = 20 * PAGE_SZ;
	uint8_t *buf = malloc(total);
	int ok;

	if (!buf) {
		perror("malloc");
		return 0;
	}
	fill_pattern(buf, total);
	ok = run_and_check(fd, buf, total, 17, total - 17 - 23, XOR_KEY,
			    "large-malloc-scattered");
	free(buf);
	return ok;
}

static int case_mmap_aligned(int fd)
{
	size_t total = 3 * PAGE_SZ;
	uint8_t *buf = mmap(NULL, total, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	int ok;

	if (buf == MAP_FAILED) {
		perror("mmap");
		return 0;
	}
	fill_pattern(buf, total);
	ok = run_and_check(fd, buf, total, 0, total, XOR_KEY, "mmap-aligned-fpo0");
	munmap(buf, total);
	return ok;
}

/* mock_no_reply: the ioctl must come back -ETIMEDOUT after roughly
 * CARFIELD_MOCK_OT_TIMEOUT_MS, not immediately and not hang forever. */
static int case_timeout(int fd)
{
	uint8_t buf[PAGE_SZ];
	struct carfield_mock_ot_req req;
	struct timespec t0, t1;
	double elapsed_ms;
	int rc, ok = 0;

	fill_pattern(buf, sizeof(buf));
	if (set_param("mock_no_reply", "1"))
		return 0;

	memset(&req, 0, sizeof(req));
	req.user_addr = (__u64)(unsigned long)buf;
	req.user_size = sizeof(buf);

	clock_gettime(CLOCK_MONOTONIC, &t0);
	rc = ioctl(fd, CARFIELD_MOCK_OT_XFORM, &req);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000.0 +
		     (t1.tv_nsec - t0.tv_nsec) / 1e6;

	if (rc == 0 || errno != ETIMEDOUT) {
		printf("FAIL [timeout]: rc=%d errno=%s (expected -1/ETIMEDOUT)\n",
		       rc, strerror(errno));
	} else if (elapsed_ms < 1000.0) {
		printf("FAIL [timeout]: returned in %.0fms, too fast for a real timeout\n",
		       elapsed_ms);
	} else if (req.mock_status != CARFIELD_MOCK_OT_STATUS_NONE) {
		printf("FAIL [timeout]: mock_status=%u, expected STATUS_NONE\n",
		       req.mock_status);
	} else {
		printf("PASS [timeout]: -ETIMEDOUT after %.0fms, no reply required\n",
		       elapsed_ms);
		ok = 1;
	}

	set_param("mock_no_reply", "0");
	return ok;
}

/* mock_corrupt_magic: the one §5 rejection case -- proves validate() itself
 * actually rejects a bad header (unreachable through the normal producer,
 * which never builds a bad one; see carfield_mock_ot.c's comment on this
 * param). */
static int case_rejection(int fd)
{
	uint8_t buf[PAGE_SZ];
	struct carfield_mock_ot_req req;
	int ok = 0;

	fill_pattern(buf, sizeof(buf));
	if (set_param("mock_corrupt_magic", "1"))
		return 0;

	memset(&req, 0, sizeof(req));
	req.user_addr = (__u64)(unsigned long)buf;
	req.user_size = sizeof(buf);

	if (ioctl(fd, CARFIELD_MOCK_OT_XFORM, &req) == 0) {
		printf("FAIL [rejection/magic]: ioctl unexpectedly succeeded\n");
	} else if (errno != EILSEQ || req.mock_status != CARFIELD_MOCK_OT_ERR_MAGIC) {
		printf("FAIL [rejection/magic]: errno=%s mock_status=%u (expected EILSEQ/%d)\n",
		       strerror(errno), req.mock_status, CARFIELD_MOCK_OT_ERR_MAGIC);
	} else {
		printf("PASS [rejection/magic]: rejected with ERR_MAGIC as a distinct errno\n");
		ok = 1;
	}

	set_param("mock_corrupt_magic", "0");
	return ok;
}

/* mock_bad_xform=1 must make the *correct-key* check fail -- this is the
 * spec's "test the test" requirement: if the real-key comparison still
 * passes here, run_and_check()'s verification would be vacuous. */
static int case_bad_xform_sanity(int fd)
{
	uint8_t buf[PAGE_SZ], shadow[PAGE_SZ];
	struct carfield_mock_ot_req req;
	size_t i;
	int mismatch = 0, ok = 0;

	fill_pattern(buf, sizeof(buf));
	memcpy(shadow, buf, sizeof(buf));

	if (set_param("mock_bad_xform", "1"))
		return 0;

	memset(&req, 0, sizeof(req));
	req.user_addr = (__u64)(unsigned long)buf;
	req.user_size = sizeof(buf);

	if (ioctl(fd, CARFIELD_MOCK_OT_XFORM, &req) < 0) {
		printf("FAIL [bad_xform sanity]: ioctl error: %s\n", strerror(errno));
		set_param("mock_bad_xform", "0");
		return 0;
	}

	for (i = 0; i < sizeof(buf); i++) {
		if (buf[i] != (uint8_t)(shadow[i] ^ XOR_KEY)) {
			mismatch = 1;
			break;
		}
	}

	if (mismatch) {
		printf("PASS [bad_xform sanity]: real-key check correctly failed (suite is not vacuous)\n");
		ok = 1;
	} else {
		printf("FAIL [bad_xform sanity]: transform matched the correct key anyway\n");
	}

	set_param("mock_bad_xform", "0");
	return ok;
}

int main(void)
{
	int fd, iter, failures = 0;

	fd = open("/dev/carfield", O_RDWR);
	if (fd < 0) {
		perror("open /dev/carfield");
		return 1;
	}

	for (iter = 1; iter <= 3; iter++) {
		printf("=== suite iteration %d/3 ===\n", iter);
		failures += !case_single_page(fd);
		failures += !case_straddle(fd);
		failures += !case_large_malloc(fd);
		failures += !case_mmap_aligned(fd);
	}

	printf("=== fault-injection cases ===\n");
	failures += !case_timeout(fd);
	failures += !case_rejection(fd);
	failures += !case_bad_xform_sanity(fd);

	close(fd);

	if (failures) {
		printf("\n%d case(s) FAILED.\n", failures);
	} else {
		printf("\nAll cases PASSED.\n");
		printf("Manual step remaining (MOCK_OT_SPEC.md §7): rmmod the module now\n");
		printf("and check `sudo dmesg` for leak/Bad-page warnings.\n");
	}

	return failures ? 1 : 0;
}
