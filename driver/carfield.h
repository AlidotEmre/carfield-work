#ifndef CARFIELD_H
#define CARFIELD_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define CARFIELD_MAGIC 'F'

/*
 * CARFIELD_PING: Phase 0 test command.
 * User space sends a value; driver echoes it back unchanged.
 * No FPGA / hardware required — validates the driver-userspace channel only.
 */
struct carfield_ping {
	__u32 value;
	__u32 echo;
};

#define CARFIELD_PING _IOWR(CARFIELD_MAGIC, 0, struct carfield_ping)

/*
 * Commands to be added in later phases:
 * #define CARFIELD_LOAD_NN    _IOW(CARFIELD_MAGIC, 1, struct carfield_nn_req)
 * #define CARFIELD_RUN        _IO (CARFIELD_MAGIC, 2)
 * #define CARFIELD_GET_RESULT _IOR(CARFIELD_MAGIC, 3, struct carfield_result)
 */

#endif /* CARFIELD_H */
