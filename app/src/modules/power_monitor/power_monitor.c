/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Power monitor module. Reads a TI INA228 or INA238 through the Zephyr sensor driver on demand.
 *
 * With an INA228 (CONFIG_APP_POWER_MONITOR_ACCUMULATORS), the Zephyr driver resets the
 * hardware accumulators every time they are fetched, so each readout yields the energy and
 * charge since the previous readout. power_monitor_update() reads the device and adds those
 * deltas to the runtime and heartbeat totals in one step, under data_lock. All readouts must
 * go through it, otherwise deltas are lost.
 *
 * The INA238 has no accumulators and is handled by the Zephyr INA237 driver. Only voltage,
 * current and power are available.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/sensor.h>
#if defined(CONFIG_APP_POWER_MONITOR_ACCUMULATORS)
#include <zephyr/drivers/sensor/ina2xx.h>
#endif /* CONFIG_APP_POWER_MONITOR_ACCUMULATORS */
#include <zephyr/logging/log.h>
#include <errno.h>

#if defined(CONFIG_MEMFAULT)
#include <memfault/metrics/metrics.h>
#endif /* CONFIG_MEMFAULT */

#include "power_monitor.h"

LOG_MODULE_REGISTER(power_monitor, CONFIG_APP_POWER_MONITOR_LOG_LEVEL);

#define POWER_MONITOR_NODE DT_NODELABEL(sip_power_monitor)

/* The INA238 has no Zephyr compatible of its own and uses the INA237 driver. */
BUILD_ASSERT(DT_NODE_HAS_COMPAT_STATUS(POWER_MONITOR_NODE, ti_ina228, okay) ||
	     DT_NODE_HAS_COMPAT_STATUS(POWER_MONITOR_NODE, ti_ina237, okay),
	     "Node label sip_power_monitor must be an enabled ti,ina228 or ti,ina237 node");

BUILD_ASSERT(IS_ENABLED(CONFIG_APP_POWER_MONITOR_ACCUMULATORS) ==
	     DT_NODE_HAS_COMPAT(POWER_MONITOR_NODE, ti_ina228),
	     "Accumulators are only available on the INA228");

/* Energy and charge accumulated over a period of time + voltage at the last readout. */
struct accumulator {
#if defined(CONFIG_APP_POWER_MONITOR_ACCUMULATORS)
	int64_t energy_uj;
	int64_t charge_uc;
#endif
	int32_t voltage_uv;
	int64_t start_ms;
};

/* Values read from the power monitor in one readout. */
struct reading {
	int32_t voltage_uv;
	int32_t current_ua;
	int32_t power_uw;
#if defined(CONFIG_APP_POWER_MONITOR_ACCUMULATORS)
	int64_t energy_uj;
	int64_t charge_uc;
#endif
};

static const struct device *const power_monitor_dev = DEVICE_DT_GET(POWER_MONITOR_NODE);

/* Protects everything below, and serializes readouts of the power monitor. */
static K_MUTEX_DEFINE(data_lock);

/* Set at init if the power monitor is present and its accumulators (if any) have been cleared */
static bool available;

/* Latest readout */
static struct reading latest;
static int64_t latest_ms;
static bool has_sample;

/* Totals */
static struct accumulator runtime;
static struct accumulator heartbeat;

#if defined(CONFIG_APP_POWER_MONITOR_ACCUMULATORS) && \
	CONFIG_APP_POWER_MONITOR_MAX_READ_INTERVAL_HOURS > 0
#define SAFETY_READ_ENABLED 1

static void safety_read_work_fn(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(safety_read_work, safety_read_work_fn);

#define SAFETY_READ_INTERVAL K_HOURS(CONFIG_APP_POWER_MONITOR_MAX_READ_INTERVAL_HOURS)
#endif /* SAFETY_READ_ENABLED */

static void accumulator_reset(struct accumulator *acc, int64_t now_ms)
{
	*acc = (struct accumulator) {
		.voltage_uv = 0,
		.start_ms = now_ms,
	};
}

static void accumulator_add(struct accumulator *acc, const struct reading *r)
{
#if defined(CONFIG_APP_POWER_MONITOR_ACCUMULATORS)
	acc->energy_uj += r->energy_uj;
	acc->charge_uc += r->charge_uc;
#endif
	acc->voltage_uv = r->voltage_uv;
}

#if defined(CONFIG_APP_POWER_MONITOR_ACCUMULATORS)
static int32_t avg_current_ua(int64_t charge_uc, int64_t duration_ms)
{
	if (duration_ms <= 0) {
		return 0;
	}

	return (int32_t)((charge_uc * MSEC_PER_SEC) / duration_ms);
}
#endif

static struct power_monitor_window accumulator_window(const struct accumulator *acc,
							int64_t end_ms)
{
	const int64_t duration_ms = MAX(0, end_ms - acc->start_ms);

	return (struct power_monitor_window) {
#if defined(CONFIG_APP_POWER_MONITOR_ACCUMULATORS)
		.energy_uj = acc->energy_uj,
		.charge_uc = acc->charge_uc,
		.avg_current_ua = avg_current_ua(acc->charge_uc, duration_ms),
#endif
		.duration_ms = duration_ms,
		.voltage_uv = acc->voltage_uv,
	};
}

static int channel_get_micro(enum sensor_channel chan, int64_t *out)
{
	struct sensor_value val;
	int err;

	err = sensor_channel_get(power_monitor_dev, chan, &val);
	if (err) {
		LOG_ERR("sensor_channel_get(%d), error: %d", chan, err);
		return err;
	}

	*out = sensor_value_to_micro(&val);

	return 0;
}

static int power_monitor_read(struct reading *r)
{
	int err;
	int64_t value;

	/* On the INA228, fetching all channels also resets the energy and charge accumulators. */
	err = sensor_sample_fetch(power_monitor_dev);
	if (err) {
		LOG_ERR("sensor_sample_fetch, error: %d", err);
		return err;
	}

	err = channel_get_micro(SENSOR_CHAN_VOLTAGE, &value);
	if (err) {
		return err;
	}

	r->voltage_uv = (int32_t)value;

	err = channel_get_micro(SENSOR_CHAN_CURRENT, &value);
	if (err) {
		return err;
	}

	r->current_ua = (int32_t)value;

	err = channel_get_micro(SENSOR_CHAN_POWER, &value);
	if (err) {
		return err;
	}

	r->power_uw = (int32_t)value;

#if defined(CONFIG_APP_POWER_MONITOR_ACCUMULATORS)
	err = channel_get_micro((enum sensor_channel)SENSOR_CHAN_INA2XX_ENERGY, &r->energy_uj);
	if (err) {
		return err;
	}

	err = channel_get_micro((enum sensor_channel)SENSOR_CHAN_INA2XX_CHARGE, &r->charge_uc);
#endif

	return err;
}

/* Read the power monitor and add the energy and charge (INA228) since the previous readout
 * to the totals.
 * Must be called with data_lock held.
 */
static int power_monitor_update_locked(void)
{
	int err;
	struct reading r;
	int64_t now_ms;
#if defined(CONFIG_APP_POWER_MONITOR_ACCUMULATORS)
	int64_t interval_ms;
#endif

	if (!available) {
		return -ENODEV;
	}

	err = power_monitor_read(&r);
	if (err) {
		/* If the fetch failed, the hardware accumulators were not reset and the next
		 * successful readout covers this interval too. If only a channel conversion
		 * failed after a successful fetch, this interval's deltas are lost.
		 */
		return err;
	}

	now_ms = k_uptime_get();
#if defined(CONFIG_APP_POWER_MONITOR_ACCUMULATORS)
	interval_ms = now_ms - latest_ms;
#endif

	latest = r;
	latest_ms = now_ms;
	has_sample = true;
	accumulator_add(&runtime, &r);
	accumulator_add(&heartbeat, &r);

#if defined(SAFETY_READ_ENABLED)
	(void)k_work_reschedule(&safety_read_work, SAFETY_READ_INTERVAL);
#endif

	if (IS_ENABLED(CONFIG_APP_POWER_MONITOR_LOG_SAMPLES)) {
#if defined(CONFIG_APP_POWER_MONITOR_ACCUMULATORS)
		LOG_INF("%d mV, %d uA, %d uW, avg %d uA over %d s",
			r.voltage_uv / 1000, r.current_ua, r.power_uw,
			avg_current_ua(r.charge_uc, interval_ms),
			(int32_t)(interval_ms / MSEC_PER_SEC));
#else
		LOG_INF("%d mV, %d uA, %d uW", r.voltage_uv / 1000, r.current_ua, r.power_uw);
#endif
	}

	return 0;
}

#if defined(SAFETY_READ_ENABLED)
static void safety_read_work_fn(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	k_mutex_lock(&data_lock, K_FOREVER);
	err = power_monitor_update_locked();
	k_mutex_unlock(&data_lock);

	if (err) {
		LOG_WRN("Safety readout failed: %d, retrying later", err);

		/* A successful readout reschedules itself; retry after a failure as well */
		(void)k_work_reschedule(&safety_read_work, SAFETY_READ_INTERVAL);
	}
}
#endif /* SAFETY_READ_ENABLED */

int power_monitor_get(struct power_monitor_data *out)
{
	int err;

	if (out == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data_lock, K_FOREVER);

	err = power_monitor_update_locked();
	if (err && has_sample) {
		LOG_WRN("Readout failed: %d, returning data from the previous readout", err);
		err = 0;
	} else if (err == -ENODEV) {
		err = -ENODATA;
	}

	if (!err) {
		*out = (struct power_monitor_data) {
			.voltage_uv = latest.voltage_uv,
			.current_ua = latest.current_ua,
			.power_uw = latest.power_uw,
			.timestamp_ms = latest_ms,
			/* The accumulators cover up to the last readout */
			.runtime = accumulator_window(&runtime, latest_ms),
			.heartbeat = accumulator_window(&heartbeat, latest_ms),
		};
	}

	k_mutex_unlock(&data_lock);

	return err;
}

int power_monitor_reset(void)
{
	int err;

	k_mutex_lock(&data_lock, K_FOREVER);
	err = power_monitor_update_locked();
	if (!err) {
		accumulator_reset(&runtime, latest_ms);
		accumulator_reset(&heartbeat, latest_ms);
	}
	k_mutex_unlock(&data_lock);

	return err;
}

void power_monitor_metrics_collect(void)
{
#if defined(CONFIG_MEMFAULT)
	int err;
	struct power_monitor_window hb;
	struct power_monitor_window rt;

	k_mutex_lock(&data_lock, K_FOREVER);

	/* Bring the totals up to now; on failure, report up to the previous readout */
	err = power_monitor_update_locked();
	if (err && err != -ENODEV) {
		LOG_WRN("Readout for the heartbeat failed: %d", err);
	}

	hb = accumulator_window(&heartbeat, latest_ms);
	rt = accumulator_window(&runtime, latest_ms);
	accumulator_reset(&heartbeat, latest_ms);

	k_mutex_unlock(&data_lock);

	if (rt.duration_ms == 0) {
		return;
	}

#if defined(CONFIG_APP_POWER_MONITOR_ACCUMULATORS)
	MEMFAULT_METRIC_SET_UNSIGNED(ina_runtime_avg_current_ua, MAX(0, rt.avg_current_ua));
	MEMFAULT_METRIC_SET_UNSIGNED(ina_runtime_energy_mj, (uint32_t)(rt.energy_uj / 1000));
#endif

	if (hb.duration_ms == 0) {
		return;
	}

#if defined(CONFIG_APP_POWER_MONITOR_ACCUMULATORS)
	MEMFAULT_METRIC_SET_UNSIGNED(ina_hb_avg_current_ua, MAX(0, hb.avg_current_ua));
	MEMFAULT_METRIC_SET_UNSIGNED(ina_hb_energy_mj, (uint32_t)(hb.energy_uj / 1000));
#endif
	MEMFAULT_METRIC_SET_UNSIGNED(ina_voltage_mv, MAX(0, hb.voltage_uv / 1000));
#endif /* CONFIG_MEMFAULT */
}

static int power_monitor_init(void)
{
	int err;
	struct reading discard;

	if (!device_is_ready(power_monitor_dev)) {
		LOG_ERR("INA228/INA238 not ready, check wiring and I2C address. "
			"Power monitor disabled");
		return 0;
	}

	k_mutex_lock(&data_lock, K_FOREVER);

	/* On the INA228 the hardware accumulators have been running since power-on. Read once to
	 * clear them so that the totals start from now.
	 */
	err = power_monitor_read(&discard);
	if (err) {
		LOG_WRN("Initial power monitor readout failed: %d", err);
	}

	latest_ms = k_uptime_get();
	accumulator_reset(&runtime, latest_ms);
	accumulator_reset(&heartbeat, latest_ms);
	available = true;

#if defined(SAFETY_READ_ENABLED)
	(void)k_work_reschedule(&safety_read_work, SAFETY_READ_INTERVAL);
#endif

	k_mutex_unlock(&data_lock);

	LOG_DBG("Power monitor started");

	return 0;
}

/* APPLICATION level: after the I2C and sensor drivers are initialized */
SYS_INIT(power_monitor_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
