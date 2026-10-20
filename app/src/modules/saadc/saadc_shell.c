/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "app_common.h"
#include "saadc.h"

LOG_MODULE_DECLARE(app_saadc, CONFIG_APP_SAADC_LOG_LEVEL);

static void saadc_shell_callback(const struct zbus_channel *chan);

/* The response is received on the application core regardless of which core samples. On the PPR
 * this runs in the zbus proxy agent receive thread.
 */
ZBUS_LISTENER_DEFINE(saadc_shell_listener, saadc_shell_callback);
ZBUS_CHAN_ADD_OBS(saadc_resp_chan, saadc_shell_listener, 0);

static K_SEM_DEFINE(response_sem, 0, 1);
static struct saadc_msg response;

static void saadc_shell_callback(const struct zbus_channel *chan)
{
	const struct saadc_msg *msg = zbus_chan_const_msg(chan);

	if (chan == &saadc_resp_chan && msg->type == SAADC_SAMPLE_RESPONSE) {
		response = *msg;
		k_sem_give(&response_sem);
	}
}

static int cmd_saadc_sample(const struct shell *sh, size_t argc, char **argv)
{
	int err;
	struct saadc_msg msg = { .type = SAADC_SAMPLE_REQUEST };

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_sem_reset(&response_sem);

	err = zbus_chan_pub(&saadc_chan, &msg, PUB_TIMEOUT);
	if (err) {
		(void)shell_print(sh, "zbus_chan_pub, error: %d", err);
		return 1;
	}

	err = k_sem_take(&response_sem, K_MSEC(CONFIG_APP_SAADC_SHELL_TIMEOUT_MS));
	if (err) {
		(void)shell_print(sh, "No response within %d ms", CONFIG_APP_SAADC_SHELL_TIMEOUT_MS);
		return 1;
	}

	(void)shell_print(sh, "raw: %d, voltage: %d mV, uptime: %lld ms",
			  response.raw, response.millivolts, response.uptime_ms);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_cmds,
			       SHELL_CMD(sample,
					 NULL,
					 "Sample the SAADC and print the result",
					 cmd_saadc_sample),
			       SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(att_saadc,
		   &sub_cmds,
		   "Asset Tracker Template SAADC module commands",
		   NULL);
