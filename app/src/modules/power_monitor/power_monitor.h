/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _POWER_MONITOR_H_
#define _POWER_MONITOR_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Totals and averages accumulated over a window, up to the last readout. */
struct power_monitor_window {
	/** Energy accumulated in the window, in microjoules. */
	int64_t energy_uj;

	/** Charge accumulated in the window, in microcoulombs. */
	int64_t charge_uc;

	/** Time span covered by energy_uj and charge_uc, in milliseconds. */
	int64_t duration_ms;

	/** Average current over the window (charge / duration), in microamperes. */
	int32_t avg_current_ua;

	/** Bus voltage at the last readout in the window, in microvolts. 0 if none yet. */
	int32_t voltage_uv;
};

/** Latest INA228 readout and the accumulated windows. */
struct power_monitor_data {
	/** Bus voltage at the readout, in microvolts. */
	int32_t voltage_uv;

	/** Current at the readout, in microamperes. Positive into the load. */
	int32_t current_ua;

	/** Power at the readout, in microwatts. */
	int32_t power_uw;

	/** Uptime of the readout, in milliseconds. */
	int64_t timestamp_ms;

	/** Since the power monitor started (boot). */
	struct power_monitor_window runtime;

	/** Since the previous Memfault heartbeat (the window in progress). */
	struct power_monitor_window heartbeat;
};

/**
 * @brief Read the INA228 and get the power monitor data, totals included up to now.
 *
 * Blocks for the I2C readout, so it must not be called from an ISR. If the readout fails,
 * the data from the previous readout is returned.
 *
 * @param[out] out Snapshot.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p out is NULL.
 * @retval -ENODATA if the INA228 is not available or no readout has succeeded yet.
 */
int power_monitor_get(struct power_monitor_data *out);

/**
 * @brief Reset the INA228 runtime totals.
 *
 * Blocks for the I2C readout, so it must not be called from an ISR.
 *
 * @retval 0 on success.
 * @retval -ENODEV if the INA228 is not available.
 * @retval other negative error codes on failure.
 */
int power_monitor_reset(void);

/**
 * @brief Read the INA228, report the heartbeat window (since the previous call) and the
 *        runtime totals as Memfault heartbeat metrics, then start a new heartbeat window.
 *
 * Intended to be called from memfault_metrics_heartbeat_collect_data(). Blocks for the I2C
 * readout.
 */
void power_monitor_metrics_collect(void);

#ifdef __cplusplus
}
#endif

#endif /* _POWER_MONITOR_H_ */
