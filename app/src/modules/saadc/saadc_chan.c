/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "saadc.h"
#if defined(CONFIG_APP_ZBUS_PROXY)
#include "zbus_proxy.h"
#endif

/* Register log module. Shared with saadc.c and saadc_shell.c. */
LOG_MODULE_REGISTER(app_saadc, CONFIG_APP_SAADC_LOG_LEVEL);

#if defined(CONFIG_BOARD_NRF9251DK_NRF9251_CPUPPR)

/* PPR core: the request originates on the application core, this is its shadow. */
ZBUS_SHADOW_CHAN_DEFINE(saadc_chan,
			struct saadc_msg,
			ppr_proxy,
			NULL,
			ZBUS_OBSERVERS_EMPTY,
			ZBUS_MSG_INIT(0)
);

/* The response originates here, forward it to the application core. */
ZBUS_CHAN_DEFINE(saadc_resp_chan,
		 struct saadc_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0)
);
ZBUS_PROXY_ADD_CHAN(ppr_proxy, saadc_resp_chan);

#else

/* Application core: the request originates here. */
ZBUS_CHAN_DEFINE(saadc_chan,
		 struct saadc_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0)
);

#if defined(CONFIG_APP_SAADC_REMOTE)

/* The implementation runs on the PPR core, forward the request and shadow the response. */
ZBUS_PROXY_ADD_CHAN(ppr_proxy, saadc_chan);

ZBUS_SHADOW_CHAN_DEFINE(saadc_resp_chan,
			struct saadc_msg,
			ppr_proxy,
			NULL,
			ZBUS_OBSERVERS_EMPTY,
			ZBUS_MSG_INIT(0)
);

#else

ZBUS_CHAN_DEFINE(saadc_resp_chan,
		 struct saadc_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0)
);

#endif /* CONFIG_APP_SAADC_REMOTE */

#endif
