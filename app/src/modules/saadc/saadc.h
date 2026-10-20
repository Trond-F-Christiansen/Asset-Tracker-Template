/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _SAADC_H_
#define _SAADC_H_

#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Channels provided by this module.
 * A channel cannot be an origin channel on both cores, so requests and responses use separate
 * channels:
 * - saadc_chan: input, always an origin channel on the application core.
 * - saadc_resp_chan: output, an origin channel on the core that runs the implementation.
 * The channels that originate on the other core are shadowed with the zbus proxy agent.
 */
ZBUS_CHAN_DECLARE(
	saadc_chan,
	saadc_resp_chan
);

enum saadc_msg_type {
	/* Output message types (saadc_resp_chan) */

	/* Response to a SAADC_SAMPLE_REQUEST. The sampled values are found in the raw and
	 * millivolts fields of the message structure.
	 */
	SAADC_SAMPLE_RESPONSE = 0x1,

	/* Input message types (saadc_chan) */

	/* Request to sample the SAADC.
	 * The response is sent as a SAADC_SAMPLE_RESPONSE message on saadc_resp_chan.
	 */
	SAADC_SAMPLE_REQUEST,
};

/* The message is copied between cores, keep it free of implicit padding and pointers. */
struct saadc_msg {
	/** One of enum saadc_msg_type. Fixed width, since the enum size differs between cores. */
	uint32_t type;

	/** Raw ADC value. Only valid for SAADC_SAMPLE_RESPONSE. */
	int32_t raw;

	/** Sampled value converted to millivolts. Only valid for SAADC_SAMPLE_RESPONSE. */
	int32_t millivolts;

	uint32_t reserved;

	/** Uptime in milliseconds of the sampling core when the sample was taken.
	 *  Only valid for SAADC_SAMPLE_RESPONSE.
	 */
	int64_t uptime_ms;
};

BUILD_ASSERT(offsetof(struct saadc_msg, uptime_ms) == 16, "saadc_msg layout must match");
BUILD_ASSERT(sizeof(struct saadc_msg) == 24, "saadc_msg layout must match on all cores");

#ifdef __cplusplus
}
#endif

#endif /* _SAADC_H_ */
