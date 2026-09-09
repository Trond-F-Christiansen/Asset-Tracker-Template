/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Central place for reading and reporting application-specific Memfault
 * heartbeat metrics. The metric keys are defined in
 * memfault/config/memfault_metrics_heartbeat_config.def.
 *
 * memfault_metrics_heartbeat_collect_data() is the Memfault per-heartbeat hook.
 * NCS normally provides it (CONFIG_MEMFAULT_NCS_IMPLEMENT_METRICS_COLLECTION);
 * that option is disabled so this module owns the hook and still triggers the
 * NCS metrics via memfault_ncs_metrics_collect_data().
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <memfault/metrics/metrics.h>
#include <memfault_ncs.h>

#if defined(CONFIG_MODEM_INFO)
#include <modem/modem_info.h>
#endif /* defined(CONFIG_MODEM_INFO) */

#if defined(CONFIG_NRF_MODEM_LIB)
#include <nrf_modem_at.h>
#endif /* defined(CONFIG_NRF_MODEM_LIB) */

#if defined(CONFIG_TEMP_NRFS)
#include <zephyr/drivers/sensor.h>
#endif /* defined(CONFIG_TEMP_NRFS) */

LOG_MODULE_REGISTER(app_memfault_metrics, CONFIG_APP_MEMFAULT_METRICS_LOG_LEVEL);

#if defined(CONFIG_MODEM_INFO)
/* IMEI is static, so report it once after the modem becomes available. */
static void imei_collect(void)
{
	static bool reported;
	char imei_buf[MODEM_INFO_MAX_RESPONSE_SIZE];
	int err;

	if (reported) {
		return;
	}

	err = modem_info_string_get(MODEM_INFO_IMEI, imei_buf, sizeof(imei_buf));
	if (err < 0) {
		/* Modem may not be initialized yet; retry on the next heartbeat. */
		LOG_WRN("Failed to read IMEI, error: %d", err);
		return;
	}

	MEMFAULT_METRIC_SET_STRING(imei, imei_buf);
	reported = true;
}
#endif /* defined(CONFIG_MODEM_INFO) */

#if defined(CONFIG_TEMP_NRFS)
/* Application-core die temperature (nRF92 nRFS temperature service). */
static void app_temp_collect(void)
{
	const struct device *const temp = DEVICE_DT_GET(DT_NODELABEL(temp_nrfs));
	struct sensor_value val;
	int err;

	if (!device_is_ready(temp)) {
		LOG_WRN("Temperature sensor not ready");
		return;
	}

	err = sensor_sample_fetch(temp);
	if (err) {
		/* -EAGAIN until the nRFS backend is ready after boot. */
		LOG_WRN("sensor_sample_fetch (temp), error: %d", err);
		return;
	}

	err = sensor_channel_get(temp, SENSOR_CHAN_DIE_TEMP, &val);
	if (err) {
		LOG_WRN("sensor_channel_get (temp), error: %d", err);
		return;
	}

	/* Store centi-degrees C; the .def scale value divides by 100. */
	int32_t centi_c = (val.val1 * 100) + (val.val2 / 10000);

	MEMFAULT_METRIC_SET_SIGNED(app_temp_c, centi_c);
}
#endif /* defined(CONFIG_TEMP_NRFS) */

#if defined(CONFIG_NRF_MODEM_LIB)
/* Modem temperature in whole degrees C via AT%XTEMP?. */
static void modem_temp_collect(void)
{
	int temp_c;
	int err;

	err = nrf_modem_at_scanf("AT%XTEMP?", "%%XTEMP: %d", &temp_c);
	if (err != 1) {
		/* Modem may be off; retry on the next heartbeat. */
		LOG_WRN("Failed to read modem temperature, error: %d", err);
		return;
	}

	MEMFAULT_METRIC_SET_SIGNED(modem_temp_c, temp_c);
}

/* Modem supply voltage in millivolts via AT%XVBAT. */
static void modem_vbat_collect(void)
{
	int vbat_mv;
	int err;

	err = nrf_modem_at_scanf("AT%XVBAT", "%%XVBAT: %d", &vbat_mv);
	if (err != 1) {
		/* Modem may be off; retry on the next heartbeat. */
		LOG_WRN("Failed to read modem VBAT, error: %d", err);
		return;
	}

	MEMFAULT_METRIC_SET_UNSIGNED(modem_vbat_mv, vbat_mv);
}
#endif /* defined(CONFIG_NRF_MODEM_LIB) */

/* Runs on every Memfault heartbeat. */
void memfault_metrics_heartbeat_collect_data(void)
{
#if defined(CONFIG_MEMFAULT_NCS_USE_DEFAULT_METRICS)
	memfault_ncs_metrics_collect_data();
#endif /* defined(CONFIG_MEMFAULT_NCS_USE_DEFAULT_METRICS) */

#if defined(CONFIG_MODEM_INFO)
	imei_collect();
#endif /* defined(CONFIG_MODEM_INFO) */

#if defined(CONFIG_NRF_MODEM_LIB)
	modem_temp_collect();
	modem_vbat_collect();
#endif /* defined(CONFIG_NRF_MODEM_LIB) */

#if defined(CONFIG_TEMP_NRFS)
	app_temp_collect();
#endif /* defined(CONFIG_TEMP_NRFS) */
}
