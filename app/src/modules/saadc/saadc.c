/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/smf.h>
#if defined(CONFIG_TASK_WDT)
#include <zephyr/task_wdt/task_wdt.h>
#endif

#include "app_common.h"
#include "saadc.h"

/* Log module is registered in saadc_chan.c */
LOG_MODULE_DECLARE(app_saadc, CONFIG_APP_SAADC_LOG_LEVEL);

/* The ADC channel is described by the first entry of the io-channels property in zephyr,user.
 * The same node is used on the application core and the PPR core.
 */
#define SAADC_NODE DT_PATH(zephyr_user)

BUILD_ASSERT(DT_NODE_HAS_PROP(SAADC_NODE, io_channels),
	     "Unsupported board: zephyr,user must have an io-channels property");

/* Register subscriber */
ZBUS_MSG_SUBSCRIBER_DEFINE(saadc);

/* Observe channels */
ZBUS_CHAN_ADD_OBS(saadc_chan, saadc, 0);

#define MAX_MSG_SIZE sizeof(struct saadc_msg)

#if defined(CONFIG_TASK_WDT)
BUILD_ASSERT(CONFIG_APP_SAADC_WATCHDOG_TIMEOUT_SECONDS >
	     CONFIG_APP_SAADC_MSG_PROCESSING_TIMEOUT_SECONDS,
	     "Watchdog timeout must be greater than maximum message processing time");
#endif

/* State machine */

/* SAADC module states.
 */
enum saadc_module_state {
	/* The module is running and waiting for sample requests */
	STATE_RUNNING,
};

/* State object.
 * Used to transfer context data between state changes.
 */
struct saadc_state_object {
	/* This must be first */
	struct smf_ctx ctx;

	/* Last channel type that a message was received on */
	const struct zbus_channel *chan;

	/* Buffer for last zbus message */
	uint8_t msg_buf[MAX_MSG_SIZE];

	/* ADC channel to sample */
	const struct adc_dt_spec adc;
};

/* Forward declarations of state handlers */
static enum smf_state_result state_running_run(void *obj);

/* State machine definition */
static const struct smf_state states[] = {
	[STATE_RUNNING] = SMF_CREATE_STATE(NULL, state_running_run, NULL, NULL, NULL),
};

static void sample_saadc(const struct adc_dt_spec *adc)
{
	int err;
	int16_t raw = 0;
	int32_t millivolts;
	struct adc_sequence sequence = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};

	err = adc_sequence_init_dt(adc, &sequence);
	if (err) {
		LOG_ERR("adc_sequence_init_dt, error: %d", err);
		SEND_FATAL_ERROR();
		return;
	}

	err = adc_read_dt(adc, &sequence);
	if (err) {
		LOG_ERR("adc_read_dt, error: %d", err);
		SEND_FATAL_ERROR();
		return;
	}

	millivolts = raw;

	err = adc_raw_to_millivolts_dt(adc, &millivolts);
	if (err) {
		LOG_ERR("adc_raw_to_millivolts_dt, error: %d", err);
		SEND_FATAL_ERROR();
		return;
	}

	struct saadc_msg msg = {
		.type = SAADC_SAMPLE_RESPONSE,
		.raw = raw,
		.millivolts = millivolts,
		.uptime_ms = k_uptime_get(),
	};

	LOG_DBG("Raw: %d, voltage: %d mV", msg.raw, msg.millivolts);

	err = zbus_chan_pub(&saadc_resp_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub, error: %d", err);
		SEND_FATAL_ERROR();
		return;
	}
}

#if defined(CONFIG_TASK_WDT)
static void saadc_wdt_callback(int channel_id, void *user_data)
{
	LOG_ERR("Watchdog expired, Channel: %d, Thread: %s",
		channel_id, k_thread_name_get((k_tid_t)user_data));

	SEND_FATAL_ERROR_WATCHDOG_TIMEOUT();
}
#endif

/* State handlers */

static enum smf_state_result state_running_run(void *obj)
{
	struct saadc_state_object const *state_object = obj;

	if (&saadc_chan == state_object->chan) {
		const struct saadc_msg *msg = (const struct saadc_msg *)state_object->msg_buf;

		if (msg->type == SAADC_SAMPLE_REQUEST) {
			LOG_DBG("SAADC sample request received, sampling");
			sample_saadc(&state_object->adc);

			return SMF_EVENT_HANDLED;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

static void saadc_module_thread(void)
{
	int err;
	k_timeout_t zbus_wait_ms = K_FOREVER;
#if defined(CONFIG_TASK_WDT)
	int task_wdt_id;
	const uint32_t wdt_timeout_ms =
		(CONFIG_APP_SAADC_WATCHDOG_TIMEOUT_SECONDS * MSEC_PER_SEC);
	const uint32_t execution_time_ms =
		(CONFIG_APP_SAADC_MSG_PROCESSING_TIMEOUT_SECONDS * MSEC_PER_SEC);

	zbus_wait_ms = K_MSEC(wdt_timeout_ms - execution_time_ms);
#endif
	/* Place state object in .data section to ensure it is captured in coredumps
	 * and can be inspected by external tools during state analysis.
	 */
	__attribute__((section(".data")))
	static struct saadc_state_object saadc_state = {
		.adc = ADC_DT_SPEC_GET_BY_IDX(SAADC_NODE, 0),
	};

	LOG_DBG("SAADC module task started");

	if (!adc_is_ready_dt(&saadc_state.adc)) {
		LOG_ERR("ADC device %s is not ready", saadc_state.adc.dev->name);
		SEND_FATAL_ERROR();
		return;
	}

	err = adc_channel_setup_dt(&saadc_state.adc);
	if (err) {
		LOG_ERR("adc_channel_setup_dt, error: %d", err);
		SEND_FATAL_ERROR();
		return;
	}

#if defined(CONFIG_TASK_WDT)
	task_wdt_id = task_wdt_add(wdt_timeout_ms, saadc_wdt_callback, (void *)k_current_get());
	if (task_wdt_id < 0) {
		LOG_ERR("Failed to add task to watchdog: %d", task_wdt_id);
		SEND_FATAL_ERROR();
		return;
	}
#endif

	smf_set_initial(SMF_CTX(&saadc_state), &states[STATE_RUNNING]);

	while (true) {
#if defined(CONFIG_TASK_WDT)
		err = task_wdt_feed(task_wdt_id);
		if (err) {
			LOG_ERR("task_wdt_feed, error: %d", err);
			SEND_FATAL_ERROR();
			return;
		}
#endif

		err = zbus_sub_wait_msg(&saadc,
					&saadc_state.chan,
					saadc_state.msg_buf,
					zbus_wait_ms);
		if (err == -ENOMSG) {
			continue;
		} else if (err) {
			LOG_ERR("zbus_sub_wait_msg, error: %d", err);
			SEND_FATAL_ERROR();
			return;
		}

		err = smf_run_state(SMF_CTX(&saadc_state));
		if (err) {
			LOG_ERR("smf_run_state(), error: %d", err);
			SEND_FATAL_ERROR();
			return;
		}
	}
}

K_THREAD_DEFINE(saadc_module_thread_id,
		CONFIG_APP_SAADC_THREAD_STACK_SIZE,
		saadc_module_thread, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
