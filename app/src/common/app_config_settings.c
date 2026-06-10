/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "app_config_settings.h"
#include "cbor_helper.h"

LOG_MODULE_REGISTER(app_config_settings, CONFIG_APP_LOG_LEVEL);

#define APP_CFG_SUBTREE       "att/cfg"
#define APP_CFG_KEY_INTERVAL  "sample_interval"
#define APP_CFG_KEY_THRESHOLD "storage_threshold"

/* Pointer to the caller-provided staging record while a load is in progress.
 * Only valid for the duration of app_config_settings_load(); the load is
 * synchronous so a static is sufficient and avoids exposing globals.
 */
static struct config_params *load_target;

static int app_cfg_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	int name_len;
	ssize_t rc;
	uint32_t val;

	if (load_target == NULL) {
		return -ENOENT;
	}

	name_len = settings_name_next(name, &next);

	if (name_len == (int)strlen(APP_CFG_KEY_INTERVAL) &&
	    !strncmp(name, APP_CFG_KEY_INTERVAL, name_len)) {
		if (len != sizeof(val)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &val, sizeof(val));
		if (rc < 0) {
			return (int)rc;
		}
		load_target->sample_interval = val;
		return 0;
	}

	if (name_len == (int)strlen(APP_CFG_KEY_THRESHOLD) &&
	    !strncmp(name, APP_CFG_KEY_THRESHOLD, name_len)) {
		if (len != sizeof(val)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &val, sizeof(val));
		if (rc < 0) {
			return (int)rc;
		}
		load_target->storage_threshold = val;
		load_target->storage_threshold_valid = true;
		return 0;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(app_cfg, APP_CFG_SUBTREE, NULL, app_cfg_set, NULL, NULL);

int app_config_settings_load(struct config_params *out)
{
	int err;

	if (out == NULL) {
		return -EINVAL;
	}

	err = settings_subsys_init();
	if (err && err != -EALREADY) {
		LOG_ERR("settings_subsys_init failed: %d", err);
		return err;
	}

	load_target = out;
	err = settings_load_subtree(APP_CFG_SUBTREE);
	load_target = NULL;

	if (err) {
		LOG_ERR("settings_load_subtree failed: %d", err);
		return err;
	}

	LOG_DBG("Loaded sample_interval=%u storage_threshold=%u (valid=%d)",
		out->sample_interval, out->storage_threshold,
		(int)out->storage_threshold_valid);

	return 0;
}

int app_config_settings_save(const struct config_params *cfg)
{
	int err;
	int first_err = 0;

	if (cfg == NULL) {
		return -EINVAL;
	}

	if (cfg->sample_interval != 0) {
		err = settings_save_one(APP_CFG_SUBTREE "/" APP_CFG_KEY_INTERVAL,
					&cfg->sample_interval, sizeof(cfg->sample_interval));
		if (err) {
			LOG_ERR("settings_save_one(sample_interval) failed: %d", err);
			first_err = first_err ? first_err : err;
		}
	}

	if (cfg->storage_threshold_valid) {
		err = settings_save_one(APP_CFG_SUBTREE "/" APP_CFG_KEY_THRESHOLD,
					&cfg->storage_threshold,
					sizeof(cfg->storage_threshold));
		if (err) {
			LOG_ERR("settings_save_one(storage_threshold) failed: %d", err);
			first_err = first_err ? first_err : err;
		}
	}

	return first_err;
}
