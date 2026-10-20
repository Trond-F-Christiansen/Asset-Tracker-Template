#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project("Asset Tracker Template PPR")

# Modules shared with the application core image, enabled by the fragments that
# app/sysbuild.cmake adds when a module is placed on the PPR core.
set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
zephyr_include_directories(${APP_SRC}/zbus_proxy)

target_sources(app PRIVATE src/main.c)

if(CONFIG_APP_ZBUS_PROXY)
  add_subdirectory(${APP_SRC}/zbus_proxy ${CMAKE_CURRENT_BINARY_DIR}/zbus_proxy)
endif()
if(CONFIG_APP_PWM)
  add_subdirectory(${APP_SRC}/modules/pwm ${CMAKE_CURRENT_BINARY_DIR}/pwm)
endif()
