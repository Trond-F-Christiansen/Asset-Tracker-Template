/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/shell/shell.h>
#include <errno.h>

#include "power_monitor.h"

static void print_window(const struct shell *shell, const char *title,
			 const struct power_monitor_window *window)
{
	shell_print(shell, "%s (%lld s):", title, (window->duration_ms / 1000));
#if defined(CONFIG_APP_POWER_MONITOR_ACCUMULATORS)
	shell_print(shell, "  Avg current:   %d uA", window->avg_current_ua);
	shell_print(shell, "  Charge:        %lld uC", window->charge_uc);
	shell_print(shell, "  Energy:        %lld uJ", window->energy_uj);
#endif /* CONFIG_APP_POWER_MONITOR_ACCUMULATORS */
}

static int cmd_power_monitor_sample(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err;
	struct power_monitor_data data;

	err = power_monitor_get(&data);
	if (err == -ENODATA) {
		shell_print(shell, "Power monitor not available");
		return 0;
	} else if (err) {
		shell_error(shell, "power_monitor_get, error: %d", err);
		return err;
	}

	shell_print(shell, "Readout:");
	shell_print(shell, "  Voltage:       %d mV", data.voltage_uv / 1000);
	shell_print(shell, "  Current:       %d uA", data.current_ua);
	shell_print(shell, "  Power:         %d uW", data.power_uw);

	print_window(shell, "Runtime", &data.runtime);

	if (IS_ENABLED(CONFIG_MEMFAULT)) {
		print_window(shell, "Heartbeat window",
			     &data.heartbeat);
	}

	return 0;
}

static int cmd_power_monitor_reset(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err = power_monitor_reset();
	if (err) {
		shell_error(shell, "power_monitor_reset, error: %d", err);
		return err;
	}

	shell_print(shell, "Power monitor runtime total reset");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_cmds,
	SHELL_CMD(sample, NULL,
		  "Read the power monitor and print the readout",
		  cmd_power_monitor_sample),
	SHELL_CMD(reset, NULL,
		  "Reset the power monitor runtime total",
		  cmd_power_monitor_reset),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(att_power_monitor,
		   &sub_cmds,
		   "Asset Tracker Template power monitor (INA228/INA238) commands",
		   NULL);
