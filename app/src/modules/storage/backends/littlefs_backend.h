/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _LITTLEFS_BACKEND_H_
#define _LITTLEFS_BACKEND_H_

#include <zephyr/shell/shell.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Dump all stored records for all data types directly from LittleFS.
 *
 * Reads every record slot — including records that have already been sent —
 * and prints them to the shell annotated with 'sent' or 'pending' status plus
 * a hex representation of the raw data.
 *
 * This function reads the filesystem directly without going through the storage
 * state machine, so it does not disturb ongoing batch sessions.
 *
 * @param sh Shell instance to print to.
 * @return 0 on success, negative errno on failure.
 */
int lfs_storage_dump_all(const struct shell *sh);

#ifdef __cplusplus
}
#endif

#endif /* _LITTLEFS_BACKEND_H_ */
