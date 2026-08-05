#ifndef ALSAQR_H
#define ALSAQR_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define ALSAQR_MAGIC 'A'

/* ── Phase 0 ─────────────────────────────────────────────────────────────── */

/*
 * ALSAQR_PING: sanity-check command — no hardware required.
 * Driver echoes value back unchanged; confirms driver-userspace channel.
 */
struct alsaqr_ping {
	__u32 value;
	__u32 echo;
};

#define ALSAQR_PING _IOWR(ALSAQR_MAGIC, 0, struct alsaqr_ping)

#endif /* ALSAQR_H */
