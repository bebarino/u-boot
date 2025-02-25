// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2011 The ChromiumOS Authors.  All rights reserved.
 *
 * Modified from the coreboot version
 */

#include <bootstage.h>
#include <cb_sysinfo.h>
#include <coreboot_tables.h>
#include <coreboot_timestamp.h>
#include <event.h>
#include <errno.h>
#include <linux/compiler.h>
#include <time.h>

void timestamp_init(void)
{
	timestamp_add_now(TS_U_BOOT_INITTED);
}

void timestamp_add(enum timestamp_id id, uint64_t ts_time)
{
	const struct sysinfo_t *info = cb_get_sysinfo();
	struct timestamp_table *ts_table = info->tstamp_table;
	struct timestamp_entry *tse;

	if (!ts_table || (ts_table->num_entries == ts_table->max_entries))
		return;

	tse = &ts_table->entries[ts_table->num_entries++];
	tse->entry_id = id;
	tse->entry_stamp = ts_time - ts_table->base_time;
}

void timestamp_add_now(enum timestamp_id id)
{
	timestamp_add(id, get_ticks());
}

static int timestamp_add_to_bootstage(void)
{
	const struct sysinfo_t *info = cb_get_sysinfo();
	const struct timestamp_table *ts_table = info->tstamp_table;
	uint i;

	if (!ts_table)
		return -ENOENT;

	for (i = 0; i < ts_table->num_entries; i++) {
		const struct timestamp_entry *tse = &ts_table->entries[i];
		const char *name = NULL;

		switch (tse->entry_id) {
		case TS_START_ROMSTAGE:
			name = "start-romstage";
			break;
		case TS_BEFORE_INITRAM:
			name = "before-initram";
			break;
		case TS_DEVICE_INITIALIZE:
			name = "device-initialize";
			break;
		case TS_DEVICE_DONE:
			name = "device-done";
			break;
		case TS_SELFBOOT_JUMP:
			name = "selfboot-jump";
			break;
		}
		if (name) {
			bootstage_add_record(0, name, BOOTSTAGEF_ALLOC,
					     tse->entry_stamp /
							(get_tbclk() / 1000000));
		}
	}

	return 0;
}
EVENT_SPY_SIMPLE(EVT_LAST_STAGE_INIT, timestamp_add_to_bootstage);
