#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# Multi-domain module compile definitions
if(CONFIG_MDM)
        target_compile_definitions(app PRIVATE "MDM_LED_PROXY_NODE=DT_NODELABEL(uart_proxy_agent)")
endif()
