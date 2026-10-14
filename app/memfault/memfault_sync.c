/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Ties Memfault heartbeats and uploads to the application's sample cycle instead of the
 * Memfault SDK's own timers (CONFIG_MEMFAULT_METRICS_HEARTBEAT_INTERVAL_SECS and
 * CONFIG_MEMFAULT_PERIODIC_UPLOAD_INTERVAL_SECS), so that the modem only wakes up once per
 * cycle.
 *
 * - CONFIG_MEMFAULT_METRICS_TIMER_CUSTOM: memfault_platform_metrics_timer_boot() below does not
 *   start a timer, so heartbeats are only taken when memfault_sync_heartbeat_trigger() is
 *   called. It uses memfault_metrics_heartbeat_debug_trigger(), which runs the same function
 *   as the SDK heartbeat timer (and the "mflt heartbeat" shell command).
 * - CONFIG_MEMFAULT_PERIODIC_UPLOAD=n: data is only uploaded when
 *   memfault_sync_upload_trigger() is called, using the same call as the periodic upload and
 *   the "mflt post_chunks" shell command.
 *
 * Both run on a dedicated workqueue: heartbeat collection performs AT commands and I2C
 * readouts, and an upload can take seconds, so neither may block the main state machine. The
 * workqueue is FIFO, so a heartbeat queued before an upload is serialized before it.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <memfault/metrics/metrics.h>
#include <memfault/metrics/platform/timer.h>
#include <memfault/core/data_packetizer.h>
#include <memfault/ports/zephyr/http.h>

#include "memfault_sync.h"

LOG_MODULE_REGISTER(memfault_sync, CONFIG_APP_MEMFAULT_METRICS_LOG_LEVEL);

BUILD_ASSERT(!IS_ENABLED(CONFIG_MEMFAULT_PERIODIC_UPLOAD),
	     "CONFIG_APP_MEMFAULT_SYNC_WITH_SAMPLING requires CONFIG_MEMFAULT_PERIODIC_UPLOAD=n");

static K_THREAD_STACK_DEFINE(memfault_sync_stack, CONFIG_APP_MEMFAULT_SYNC_STACK_SIZE);
static struct k_work_q memfault_sync_work_q;

static void heartbeat_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_DBG("Memfault heartbeat");
	memfault_metrics_heartbeat_debug_trigger();
}

static void upload_work_fn(struct k_work *work)
{
	ssize_t rv;

	ARG_UNUSED(work);

	if (!memfault_packetizer_data_available()) {
		LOG_DBG("No Memfault data to upload");
		return;
	}

	rv = memfault_zephyr_port_post_data_return_size();
	if (rv < 0) {
		LOG_WRN("Memfault upload failed: %d", (int)rv);
	} else {
		LOG_DBG("Uploaded %d bytes of Memfault data", (int)rv);
	}
}

static K_WORK_DEFINE(heartbeat_work, heartbeat_work_fn);
static K_WORK_DEFINE(upload_work, upload_work_fn);

/* Called by the Memfault SDK at boot. Do not start a timer: heartbeats are triggered by the
 * application through memfault_sync_heartbeat_trigger().
 */
bool memfault_platform_metrics_timer_boot(uint32_t period_sec,
					  MemfaultPlatformTimerCallback callback)
{
	ARG_UNUSED(period_sec);
	ARG_UNUSED(callback);

	return true;
}

void memfault_sync_heartbeat_trigger(void)
{
	(void)k_work_submit_to_queue(&memfault_sync_work_q, &heartbeat_work);
}

void memfault_sync_upload_trigger(void)
{
	(void)k_work_submit_to_queue(&memfault_sync_work_q, &upload_work);
}

static int memfault_sync_init(void)
{
	const struct k_work_queue_config config = {
		.name = "mflt_sync",
	};

	k_work_queue_start(&memfault_sync_work_q, memfault_sync_stack,
			   K_THREAD_STACK_SIZEOF(memfault_sync_stack),
			   K_LOWEST_APPLICATION_THREAD_PRIO, &config);

	return 0;
}

/* Before the application threads start publishing sample triggers */
SYS_INIT(memfault_sync_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
