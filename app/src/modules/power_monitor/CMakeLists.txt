#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/power_monitor.c)
target_sources_ifdef(CONFIG_APP_POWER_MONITOR_SHELL app PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/power_monitor_shell.c
)
target_include_directories(app PRIVATE .)
