/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _PWM_H_
#define _PWM_H_

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Channels provided by this module.
 * pwm_chan is always an origin channel on the application core. If the module runs on the PPR
 * core, it is forwarded there with the zbus proxy agent.
 */
ZBUS_CHAN_DECLARE(
	pwm_chan
);

enum pwm_msg_type {
	/* Input message types */

	/* Start the PWM output with the last configured period and pulse width. */
	PWM_ON = 0x1,

	/* Stop the PWM output. The configured period and pulse width are kept. */
	PWM_OFF,

	/* Configure the PWM output and start it. The values are found in the period_usec and
	 * pulse_usec fields of the message structure.
	 */
	PWM_SET,
};

/* The message is copied between cores, keep it free of padding and pointers. */
struct pwm_msg {
	/** One of enum pwm_msg_type. Fixed width, since the enum size differs between cores. */
	uint32_t type;

	/** PWM period in microseconds. Only valid for PWM_SET. */
	uint32_t period_usec;

	/** PWM pulse width in microseconds. Only valid for PWM_SET. */
	uint32_t pulse_usec;
};

BUILD_ASSERT(sizeof(struct pwm_msg) == 12, "pwm_msg layout must match on all cores");

#ifdef __cplusplus
}
#endif

#endif /* _PWM_H_ */
