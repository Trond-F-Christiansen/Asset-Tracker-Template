/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "pwm.h"

/* The PWM output is described by the first entry of the pwms property in zephyr,user.
 * The same node is used on the application core and the PPR core.
 */
#define PWM_NODE DT_PATH(zephyr_user)

BUILD_ASSERT(DT_NODE_HAS_PROP(PWM_NODE, pwms),
	     "Unsupported board: zephyr,user must have a pwms property");

static const struct pwm_dt_spec pwm_out = PWM_DT_SPEC_GET(PWM_NODE);

/* Log module is registered in pwm_chan.c */
LOG_MODULE_DECLARE(app_pwm, CONFIG_APP_PWM_LOG_LEVEL);

static void pwm_callback(const struct zbus_channel *chan);

/* Register listener - pwm_callback will be called every time a channel that the module listens on
 * receives a new message. On the PPR core this runs in the zbus proxy agent receive thread.
 */
ZBUS_LISTENER_DEFINE(pwm_listener, pwm_callback);

/* Observe channels */
ZBUS_CHAN_ADD_OBS(pwm_chan, pwm_listener, 0);

static uint32_t period_usec = CONFIG_APP_PWM_DEFAULT_PERIOD_USEC;
static uint32_t pulse_usec = CONFIG_APP_PWM_DEFAULT_PULSE_USEC;

static int pwm_apply(uint32_t period, uint32_t pulse)
{
	int err;

	if (!pwm_is_ready_dt(&pwm_out)) {
		LOG_ERR("PWM device %s is not ready", pwm_out.dev->name);
		return -ENODEV;
	}

	err = pwm_set_dt(&pwm_out, PWM_USEC(period), PWM_USEC(pulse));
	if (err) {
		LOG_ERR("pwm_set_dt, error: %d", err);
		return err;
	}

	return 0;
}

/* Function called when there is a message received on a channel that the module listens to */
static void pwm_callback(const struct zbus_channel *chan)
{
	const struct pwm_msg *msg;

	if (chan != &pwm_chan) {
		return;
	}

	msg = zbus_chan_const_msg(chan);

	switch (msg->type) {
	case PWM_ON:
		LOG_DBG("PWM_ON, period: %u us, pulse: %u us", period_usec, pulse_usec);
		(void)pwm_apply(period_usec, pulse_usec);
		break;
	case PWM_OFF:
		LOG_DBG("PWM_OFF");
		(void)pwm_apply(period_usec, 0);
		break;
	case PWM_SET:
		LOG_DBG("PWM_SET, period: %u us, pulse: %u us", msg->period_usec, msg->pulse_usec);

		if (msg->period_usec == 0 || msg->pulse_usec > msg->period_usec) {
			LOG_ERR("Invalid PWM_SET, period: %u us, pulse: %u us",
				msg->period_usec, msg->pulse_usec);
			break;
		}

		if (pwm_apply(msg->period_usec, msg->pulse_usec) == 0) {
			period_usec = msg->period_usec;
			pulse_usec = msg->pulse_usec;
		}
		break;
	default:
		LOG_WRN("Unknown message type: %u", msg->type);
		break;
	}
}
