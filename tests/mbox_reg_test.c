/*
 * Userspace test for the AlSaqr mailbox register map (driver/alsaqr_mbox_hw.h).
 *
 * Pure arithmetic/constant checks against the real, generated device-tree
 * node (alsaqr-fpga-ecs/dts/generate_dts.py):
 *
 *   ot_mbox@10404000 {
 *     compatible = "opentitan_mbox-0.0";
 *     reg = <0x0 0x10404000 0x0 0x28>;
 *     interrupt-parent = <&PLIC0>;
 *     interrupts = <10>;
 *   };
 *
 * No kernel, no FPGA, no hardware required -- this is the one part of the
 * real mailbox backend that's fully testable without silicon.
 */

#include <stdio.h>
#include <string.h>
#include "../driver/alsaqr_mbox_hw.h"

static int failures;

static void check_addr(const char *name, unsigned long got, unsigned long expected)
{
	printf("[%-24s] -> 0x%02lx\n", name, got);
	if (got != expected) {
		printf("    FAIL: expected 0x%02lx\n", expected);
		failures++;
	}
}

int main(void)
{
	printf("== AlSaqr mailbox register-map test (no kernel/FPGA needed) ==\n\n");

	/* Base address and total span must match the DT node exactly. */
	check_addr("MBOX_BASE_ADDR", ALSAQR_MBOX_BASE_ADDR, 0x10404000UL);
	check_addr("MBOX_UNIT_SIZE", ALSAQR_MBOX_UNIT_SIZE, 0x28);

	/* Word area (outbound header_phys + cmd) must fit inside the word area
	 * reserved for it, and the word area itself must fit before DOORBELL. */
	check_addr("WORD0",             ALSAQR_MBOX_REG_WORD0, 0x00);
	check_addr("WORD1",             ALSAQR_MBOX_REG_WORD1, 0x04);
	check_addr("WORD_AREA_SIZE",    ALSAQR_MBOX_WORD_AREA_SIZE, 0x14);

	/* Trigger registers -- these offsets are the two hardware addresses
	 * this driver actually pokes beyond the word area, taken directly
	 * from titanssl_driver/driver.c's DOORBELL/COMPLETION #defines, which
	 * already run against this same base in production. */
	check_addr("DOORBELL",    ALSAQR_MBOX_REG_DOORBELL,    0x20);
	check_addr("COMPLETION",  ALSAQR_MBOX_REG_COMPLETION,  0x24);

	printf("[%-24s] \"%s\"\n", "DT_COMPATIBLE", ALSAQR_MBOX_DT_COMPATIBLE);
	if (strcmp(ALSAQR_MBOX_DT_COMPATIBLE, "opentitan_mbox-0.0") != 0) {
		printf("    FAIL: does not match the generated DT node's compatible string\n");
		failures++;
	}

	/* Sanity: every register this driver touches must fall inside the
	 * ioremap'd window, and the window itself must match the DT reg size
	 * exactly (not just "big enough") so a future DT regen that shrinks
	 * or grows the node is caught here instead of silently over/under
	 * mapping real hardware. */
	if (ALSAQR_MBOX_REG_WORD0 + 4 > ALSAQR_MBOX_UNIT_SIZE ||
	    ALSAQR_MBOX_REG_WORD1 + 4 > ALSAQR_MBOX_UNIT_SIZE ||
	    ALSAQR_MBOX_REG_DOORBELL + 4 > ALSAQR_MBOX_UNIT_SIZE ||
	    ALSAQR_MBOX_REG_COMPLETION + 4 > ALSAQR_MBOX_UNIT_SIZE) {
		printf("    FAIL: a register offset falls outside ALSAQR_MBOX_UNIT_SIZE\n");
		failures++;
	}
	if (ALSAQR_MBOX_WORD_AREA_SIZE > ALSAQR_MBOX_REG_DOORBELL) {
		printf("    FAIL: word area overlaps the doorbell register\n");
		failures++;
	}

	printf("\n");
	if (failures) {
		printf("== FAIL: %d case(s) failed ==\n", failures);
		return 1;
	}
	printf("== PASS: all cases matched ==\n");
	return 0;
}
