#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# Shared by the application core image and the PPR image.
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pwm_chan.c)
target_sources_ifndef(CONFIG_APP_PWM_REMOTE app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pwm.c)
target_sources_ifdef(CONFIG_APP_PWM_SHELL app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pwm_shell.c)
target_include_directories(app PRIVATE .)
