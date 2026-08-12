/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void)
{
	uint32_t iter = 0;

	while (1) {
		if (iter == 0) {
			printk("att_ppr_heartbeat: alive\n");
		}

		printk("att_ppr_heartbeat: uptime: %llu ms\n", k_uptime_get());

		iter++;

		k_sleep(K_SECONDS(1));
	}

	return 0;
}
