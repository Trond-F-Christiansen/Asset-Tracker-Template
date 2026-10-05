#
# Copyright (c) 2025-2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# SB_CONFIG_LAUNCH_PPR: add the application core side of the PPR log forwarding
# (IPC log link + PPR launch) to the default image. The PPR side lives in the
# ppr image's own prj.conf/app.overlay.
#
# A defined <image>_EXTRA_CONF_FILE / <image>_EXTRA_DTC_OVERLAY_FILE replaces the
# unprefixed -DEXTRA_CONF_FILE / -DEXTRA_DTC_OVERLAY_FILE for the main image
# instead of merging with it, so carry the unprefixed value over first.
if(SB_CONFIG_LAUNCH_PPR)
  sysbuild_cache_set(VAR ${DEFAULT_IMAGE}_EXTRA_CONF_FILE APPEND REMOVE_DUPLICATES
                     ${EXTRA_CONF_FILE} ${APP_DIR}/ppr/cpuapp_ipc_log.conf)
  sysbuild_cache_set(VAR ${DEFAULT_IMAGE}_EXTRA_DTC_OVERLAY_FILE APPEND REMOVE_DUPLICATES
                     ${EXTRA_DTC_OVERLAY_FILE} ${APP_DIR}/ppr/cpuapp_ipc_log.overlay)
endif()
