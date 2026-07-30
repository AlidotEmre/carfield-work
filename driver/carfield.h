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
 * IOCTL to notify OpenTitan to boot the PULP cluster from boot_addr.
 *
 * The host cannot boot the cluster directly -- OpenTitan is the only
 * thing that can, and how it does so is out of scope for the host driver
 * (Daniele's 2026-07-30 code review; see memory/project_alsaqr.md). This
 * ioctl's job is just the host<->OT notification, over the same
 * letter0/letter1 mailbox seam as CARFIELD_MOCK_OT_XFORM (see
 * carfield_mock_ot.h's CARFIELD_MOCK_OT_CMD_CLUSTER_BOOT).
 *
 * num_cores was dropped from this struct: booting the cluster is now
 * entirely OpenTitan's business (black box), so how many cores to
 * release is no longer something the host decides.
 *
 * boot_addr : physical L2 address where ELF entry point lives (e.g. 0x78000000)
 * result    : OT's reply status word (output field). OPEN QUESTION
 *             (docs/QUESTIONS_FOR_TEAM.md): unconfirmed whether this same
 *             reply also means "cluster finished running", or only "OT
 *             accepted the boot request" -- treated as the former for now.
 */
struct carfield_cluster_run {
	__u32 boot_addr;
	__u32 result;
};

#define CARFIELD_CLUSTER_RUN _IOWR(CARFIELD_MAGIC, 1, struct carfield_cluster_run)

#endif /* CARFIELD_H */
