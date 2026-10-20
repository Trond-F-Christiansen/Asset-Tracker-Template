/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>

#include "app_common.h"
#include "pwm.h"

LOG_MODULE_DECLARE(app_pwm, CONFIG_APP_PWM_LOG_LEVEL);

static int pwm_shell_pub(const struct shell *sh, const struct pwm_msg *msg)
{
	int err = zbus_chan_pub(&pwm_chan, msg, PUB_TIMEOUT);

	if (err) {
		(void)shell_print(sh, "zbus_chan_pub, error: %d", err);
		return 1;
	}

	return 0;
}

static int cmd_pwm_on(const struct shell *sh, size_t argc, char **argv)
{
	struct pwm_msg msg = { .type = PWM_ON };

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return pwm_shell_pub(sh, &msg);
}

static int cmd_pwm_off(const struct shell *sh, size_t argc, char **argv)
{
	struct pwm_msg msg = { .type = PWM_OFF };

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return pwm_shell_pub(sh, &msg);
}

static int cmd_pwm_set(const struct shell *sh, size_t argc, char **argv)
{
	int err = 0;
	struct pwm_msg msg = { .type = PWM_SET };

	if (argc != 3) {
		(void)shell_print(sh, "Invalid number of arguments (%d)", argc);
		(void)shell_print(sh, "Usage: att_pwm set <period_us> <pulse_us>");
		return 1;
	}

	msg.period_usec = (uint32_t)shell_strtoul(argv[1], 10, &err);
	if (err) {
		(void)shell_print(sh, "Invalid period: %s", argv[1]);
		return 1;
	}

	msg.pulse_usec = (uint32_t)shell_strtoul(argv[2], 10, &err);
	if (err) {
		(void)shell_print(sh, "Invalid pulse: %s", argv[2]);
		return 1;
	}

	if (msg.period_usec == 0 || msg.pulse_usec > msg.period_usec) {
		(void)shell_print(sh, "Period must be > 0 and pulse must not exceed period");
		return 1;
	}

	return pwm_shell_pub(sh, &msg);
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_cmds,
			       SHELL_CMD(on,
					 NULL,
					 "Start PWM with the last configured values",
					 cmd_pwm_on),
			       SHELL_CMD(off,
					 NULL,
					 "Stop PWM",
					 cmd_pwm_off),
			       SHELL_CMD(set,
					 NULL,
					 "Configure and start PWM. Usage: set <period_us> <pulse_us>",
					 cmd_pwm_set),
			       SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(att_pwm,
		   &sub_cmds,
		   "Asset Tracker Template PWM module commands",
		   NULL);
