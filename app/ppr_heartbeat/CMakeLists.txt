#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

# Same board root as the main ATT image so sysbuild child images resolve SHIELD=...
cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH _att_app_dir)
list(APPEND BOARD_ROOT "${_att_app_dir}")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(att_ppr_heartbeat)

target_sources(app PRIVATE src/main.c)
