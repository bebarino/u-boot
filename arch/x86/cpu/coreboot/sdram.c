// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2011 The Chromium OS Authors.
 * (C) Copyright 2010,2011
 * Graeme Russ, <graeme.russ@gmail.com>
 */

#include <cb_sysinfo.h>
#include <init.h>
#include <asm/e820.h>
#include <asm/global_data.h>

unsigned int install_e820_map(unsigned int max_entries,
			      struct e820_entry *entries)
{
	return cb_install_e820_map(max_entries, entries);
}

phys_addr_t board_get_usable_ram_top(phys_size_t total_size)
{
	return coreboot_board_get_usable_ram_top(total_size);
}

int dram_init(void)
{
	return coreboot_dram_init()
}

int dram_init_banksize(void)
{
	return coreboot_dram_init_banksize();
}
