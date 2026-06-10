/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_CONFIG_SETTINGS_H_
#define APP_CONFIG_SETTINGS_H_

#include "cbor_helper.h"

/**
 * @brief Initialize the settings subsystem and load any persisted device
 *        configuration into @p out.
 *
 * Fields in @p out that are not present in non-volatile storage are left
 * untouched, so callers can pre-populate it with Kconfig defaults.
 *
 * @param[in,out] out Configuration parameters to populate from storage.
 *
 * @retval 0 Success.
 * @retval -EINVAL @p out is NULL.
 * @retval Negative errno from settings_subsys_init() or settings_load_subtree().
 */
int app_config_settings_load(struct config_params *out);

/**
 * @brief Persist the device configuration to non-volatile storage.
 *
 * Only fields considered valid (non-zero @c sample_interval and, when
 * @c storage_threshold_valid is true, @c storage_threshold) are written.
 *
 * @param[in] cfg Configuration parameters to persist.
 *
 * @retval 0 Success.
 * @retval -EINVAL @p cfg is NULL.
 * @retval Negative errno from settings_save_one().
 */
int app_config_settings_save(const struct config_params *cfg);

#endif /* APP_CONFIG_SETTINGS_H_ */
