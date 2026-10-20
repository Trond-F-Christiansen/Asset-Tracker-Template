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

# SB_CONFIG_ATT_PWM_ON_PPR: run the module implementation on the PPR core. Both images get the
# zbus proxy agent fragments, and the application core image gets the fragments that hand the
# peripheral over to the PPR image. The ppr image is selected by SB_CONFIG_PPRCORE_IMAGE_NAME.
if(SB_CONFIG_ATT_PWM_ON_PPR)
  set(ppr_image ${SB_CONFIG_PPRCORE_IMAGE_NAME})
  # The modules select the proxy agent in both images, so the application core image needs no
  # proxy conf fragment, only the devicetree one.
  set(app_conf ${APP_DIR}/src/modules/pwm/remote/app.conf)
  set(app_overlay ${APP_DIR}/src/zbus_proxy/remote/app.overlay
                  ${APP_DIR}/src/modules/pwm/remote/app.overlay)
  set(ppr_conf ${APP_DIR}/src/zbus_proxy/remote/ppr.conf
               ${APP_DIR}/src/modules/pwm/remote/ppr.conf)
  set(ppr_overlay ${APP_DIR}/src/zbus_proxy/remote/ppr.overlay
                  ${APP_DIR}/src/modules/pwm/remote/ppr.overlay)

  # Same rule as above: carry over the unprefixed values and the fragments added by
  # SB_CONFIG_LAUNCH_PPR before appending.
  sysbuild_cache_set(VAR ${DEFAULT_IMAGE}_EXTRA_CONF_FILE APPEND REMOVE_DUPLICATES
                     ${EXTRA_CONF_FILE} ${app_conf})
  sysbuild_cache_set(VAR ${DEFAULT_IMAGE}_EXTRA_DTC_OVERLAY_FILE APPEND REMOVE_DUPLICATES
                     ${EXTRA_DTC_OVERLAY_FILE} ${app_overlay})
  sysbuild_cache_set(VAR ${ppr_image}_EXTRA_CONF_FILE APPEND REMOVE_DUPLICATES ${ppr_conf})
  sysbuild_cache_set(VAR ${ppr_image}_EXTRA_DTC_OVERLAY_FILE APPEND REMOVE_DUPLICATES
                     ${ppr_overlay})
endif()
