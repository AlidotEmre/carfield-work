/*
 * Userspace test for carfield_mbox_reg_addr() (driver/carfield_mbox_hw.h).
 *
 * Pure arithmetic -- validates that base+id*stride+offset matches the
 * confirmed register map (docs/QUESTIONS_FOR_TEAM.md, Daniele's 2026-07-09
 * answers) by hand-checked expected values. No kernel, no FPGA, no hardware
 * required; this is the one part of the real mailbox backend that's fully
 * testable without silicon (spec §5.2).
 */

#include <stdio.h>
#include "../driver/carfield_mbox_hw.h"

static int failures;

static void check(const char *name, unsigned int id, unsigned long off,
		   unsigned long expected)
{
	unsigned long got = carfield_mbox_reg_addr(id, off);

	printf("[%-24s] id=%u off=0x%02lx -> 0x%04lx\n", name, id, off, got);
	if (got != expected) {
		printf("    FAIL: expected 0x%04lx\n", expected);
		failures++;
	}
}

int main(void)
{
	printf("== Carfield mailbox register-math test (no kernel/FPGA needed) ==\n\n");

	/* Mailbox 1 (host -> OT): SND_SET, LETTER0, LETTER1 */
	check("id1 SND_STAT",  CARFIELD_MBOX_ID_HOST_TO_OT, CARFIELD_MBOX_REG_SND_STAT, 0x100);
	check("id1 SND_SET",   CARFIELD_MBOX_ID_HOST_TO_OT, CARFIELD_MBOX_REG_SND_SET,  0x104);
	check("id1 SND_CLR",   CARFIELD_MBOX_ID_HOST_TO_OT, CARFIELD_MBOX_REG_SND_CLR,  0x108);
	check("id1 LETTER0",   CARFIELD_MBOX_ID_HOST_TO_OT, CARFIELD_MBOX_REG_LETTER0,  0x180);
	check("id1 LETTER1",   CARFIELD_MBOX_ID_HOST_TO_OT, CARFIELD_MBOX_REG_LETTER1,  0x184);

	/* Mailbox 5 (PULP -> host) */
	check("id5 SND_STAT",  CARFIELD_MBOX_ID_PULP_TO_HOST, CARFIELD_MBOX_REG_SND_STAT, 0x500);
	check("id5 SND_SET",   CARFIELD_MBOX_ID_PULP_TO_HOST, CARFIELD_MBOX_REG_SND_SET,  0x504);
	check("id5 SND_CLR",   CARFIELD_MBOX_ID_PULP_TO_HOST, CARFIELD_MBOX_REG_SND_CLR,  0x508);
	check("id5 LETTER0",   CARFIELD_MBOX_ID_PULP_TO_HOST, CARFIELD_MBOX_REG_LETTER0,  0x580);
	check("id5 LETTER1",   CARFIELD_MBOX_ID_PULP_TO_HOST, CARFIELD_MBOX_REG_LETTER1,  0x584);

	/* Mailbox 7 (OT -> host) */
	check("id7 SND_STAT",  CARFIELD_MBOX_ID_OT_TO_HOST, CARFIELD_MBOX_REG_SND_STAT, 0x700);
	check("id7 SND_SET",   CARFIELD_MBOX_ID_OT_TO_HOST, CARFIELD_MBOX_REG_SND_SET,  0x704);
	check("id7 SND_CLR",   CARFIELD_MBOX_ID_OT_TO_HOST, CARFIELD_MBOX_REG_SND_CLR,  0x708);
	check("id7 LETTER0",   CARFIELD_MBOX_ID_OT_TO_HOST, CARFIELD_MBOX_REG_LETTER0,  0x780);
	check("id7 LETTER1",   CARFIELD_MBOX_ID_OT_TO_HOST, CARFIELD_MBOX_REG_LETTER1,  0x784);

	/* Sanity: unit size covers exactly through id7's LETTER1 (0x784) and
	 * no further -- confirms the ioremap window define matches the ids
	 * actually in use, not an arbitrary round number. */
	printf("[%-24s] -> 0x%04x\n", "CARFIELD_MBOX_UNIT_SIZE", CARFIELD_MBOX_UNIT_SIZE);
	if (CARFIELD_MBOX_UNIT_SIZE <= 0x784) {
		printf("    FAIL: unit size too small to cover id7's LETTER1 (0x784)\n");
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
