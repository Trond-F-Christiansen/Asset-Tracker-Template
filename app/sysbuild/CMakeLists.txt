#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

find_package(Sysbuild REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(sysbuild LANGUAGES)

# Pad the SB_CONFIG_MERGED_HEX_FILES outputs in place, so J-Link can program
# them into nRF92 MRAM (see SB_CONFIG_ATT_MERGED_HEX_PAD). The
# merged_<board_target>_target targets are created by find_package(Sysbuild)
# above, in this directory.
if(SB_CONFIG_ATT_MERGED_HEX_PAD)
  set(att_pad_script ${APP_DIR}/../scripts/pad_hex.py)

  get_property(att_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target ${att_targets})
    if(target MATCHES "^merged_(.+)_target$")
      set(merged_hex ${CMAKE_BINARY_DIR}/merged_${CMAKE_MATCH_1}.hex)
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${PYTHON_EXECUTABLE} ${att_pad_script} ${merged_hex} ${merged_hex}
        COMMENT "Padding merged_${CMAKE_MATCH_1}.hex to 16-byte MRAM lines"
        VERBATIM
      )
    endif()
  endforeach()
endif()
