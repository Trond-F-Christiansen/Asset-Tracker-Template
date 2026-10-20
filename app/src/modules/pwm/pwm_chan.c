/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "pwm.h"
#if defined(CONFIG_APP_ZBUS_PROXY)
#include "zbus_proxy.h"
#endif

/* Register log module. Shared with pwm.c and pwm_shell.c. */
LOG_MODULE_REGISTER(app_pwm, CONFIG_APP_PWM_LOG_LEVEL);

#if defined(CONFIG_BOARD_NRF9251DK_NRF9251_CPUPPR)

/* PPR core: pwm_chan originates on the application core, this is its shadow. */
ZBUS_SHADOW_CHAN_DEFINE(pwm_chan,
			struct pwm_msg,
			ppr_proxy,
			NULL,
			ZBUS_OBSERVERS_EMPTY,
			ZBUS_MSG_INIT(0)
);

#else

/* Application core: pwm_chan is the origin channel. */
ZBUS_CHAN_DEFINE(pwm_chan,
		 struct pwm_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0)
);

#if defined(CONFIG_APP_PWM_REMOTE)
/* The implementation runs on the PPR core, forward the channel. */
ZBUS_PROXY_ADD_CHAN(ppr_proxy, pwm_chan);
#endif

#endif
