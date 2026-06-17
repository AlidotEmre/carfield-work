/*
 * Hello World for the PULP integer cluster on Carfield SoC.
 *
 * Runs ON the cluster (not on CVA6). All 12 cores enter main(); only
 * core 0 prints. All cores must reach the barrier before the cluster
 * signals EOC to the Linux driver.
 *
 * Build requirements:
 *   - Toolchain : riscv32-unknown-elf-gcc (PULP variant)
 *   - Runtime   : pulp-runtime (https://github.com/pulp-platform/pulp-runtime)
 *   - SDK entry : PULPD_ROOT pointing to pulp-runtime regression_tests/carfield/
 *
 * Build command (from pulp-runtime project):
 *   make all
 *
 * The resulting ELF must be stripped of L1 cluster sections before loading:
 *   riscv32-unknown-elf-objcopy \
 *       --remove-section .l1cluster_g \
 *       --remove-section .bss_l1      \
 *       hello.elf
 *
 * The Linux driver loads this binary to L2 (0x78000000) and sets the
 * cluster boot address to ELF_BOOT_ADDR before asserting BOOT_ENABLE + FETCH_ENABLE.
 */

#include <stdio.h>
#include "rt/rt_api.h"   /* pulp-runtime: rt_core_id(), rt_team_barrier() */

int main(void)
{
	int core_id = rt_core_id();

	if (core_id == 0)
		printf("Hello from PULP cluster! Core 0 of %d reporting.\n",
		       rt_nb_pe());

	/* All cores rendezvous here before cluster signals EOC. */
	rt_team_barrier();

	return 0;
}
