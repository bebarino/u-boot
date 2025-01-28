// SPDX-License-Identifier: GPL-2.0+

#include <cb_sysinfo.h>
#include <linux/errno.h>
#include <linux/sizes.h>
#include <log.h>

long locate_coreboot_table(void)
{
	long addr;

	/*
	 * We look for LBIO somewhere inside the CBMEM arena, which is
	 * typically at the top of the first 4G of RAM.
	 */
	addr = detect_coreboot_table_at(SZ_4G - SZ_8M, SZ_8M);
	if (addr < 0)
		return -ENOENT;

	debug("Located coreboot table at %#lx\n", addr);
	return addr;
}

int arch_cpu_init(void)
{
	int ret;

	ret = get_coreboot_info(&lib_sysinfo);
	if (ret != 0)
		debug("Failed to parse coreboot tables.\n");

	return 0;
}
