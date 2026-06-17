#ifndef CARFIELD_H
#define CARFIELD_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define CARFIELD_MAGIC 'F'

/* ── Phase 0 ─────────────────────────────────────────────────────────────── */

/*
 * CARFIELD_PING: sanity-check command — no hardware required.
 * Driver echoes value back unchanged; confirms driver-userspace channel.
 */
struct carfield_ping {
	__u32 value;
	__u32 echo;
};

#define CARFIELD_PING _IOWR(CARFIELD_MAGIC, 0, struct carfield_ping)

/* ── Phase 2 ─────────────────────────────────────────────────────────────── */

/*
 * mmap page offsets — must match car_linux_mmap.h from the Carfield repo.
 * User space calls mmap(fd, offset * PAGE_SIZE) to get a virtual window
 * into each hardware region. Binary loading uses MMAP_L2_INTL_0.
 */
#define CARFIELD_MMAP_SOC_CTRL      0
#define CARFIELD_MMAP_L2_INTL_0     10
#define CARFIELD_MMAP_L2_CONT_0     11
#define CARFIELD_MMAP_L2_INTL_1     12
#define CARFIELD_MMAP_L2_CONT_1     13
#define CARFIELD_MMAP_SAFETY_ISLAND 100
#define CARFIELD_MMAP_INT_CLUSTER   200
#define CARFIELD_MMAP_SPATZ_CLUSTER 300

/*
 * CARFIELD_CLUSTER_RUN: load binary into L2 (via mmap), then issue this
 * IOCTL to set the boot address, release reset, and wait for EOC.
 *
 * boot_addr : physical L2 address where ELF entry point lives (e.g. 0x78000000)
 * num_cores : cores to boot; 0 means all 12
 * result    : return value written by the cluster (output field)
 */
struct carfield_cluster_run {
	__u32 boot_addr;
	__u32 num_cores;
	__u32 result;
};

#define CARFIELD_CLUSTER_RUN _IOWR(CARFIELD_MAGIC, 1, struct carfield_cluster_run)

#endif /* CARFIELD_H */
