/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/devicetree.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/zbus/proxy_agent/zbus_proxy_agent.h>
#include <zephyr/zbus/proxy_agent/zbus_proxy_agent_ipc.h>

#include "zbus_proxy.h"

/* The IPC instance is selected with zephyr,user:zbus-proxy-ipc (remote/ipc.dtsi), added by
 * app/sysbuild.cmake (remote/app.overlay and remote/ppr.overlay).
 */
ZBUS_PROXY_AGENT_DEFINE(ppr_proxy,
			ZBUS_PROXY_AGENT_BACKEND_IPC,
			DT_PHANDLE(DT_PATH(zephyr_user), zbus_proxy_ipc));
