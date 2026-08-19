/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _CLOUD_LOCATION_H_
#define _CLOUD_LOCATION_H_

#include "location.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle location-related messages from the location module.
 *
 * Processes location requests, A-GNSS requests, and GNSS location data.
 *
 * @param msg Pointer to location message
 *
 * @retval 0 on success or a negative errno on failure.
 */
int cloud_location_handle_message(const struct location_msg *msg);

/**
 * @brief Build and send a cloud (cell/Wi-Fi) location request as a timestamped device message.
 *
 * Sends a GROUND_FIX device message so the resolved fix is associated with @p ts_ms rather than
 * the time of receipt. do_reply is disabled; nRF Cloud stores the resolved location.
 *
 * @param request     Cellular/Wi-Fi scan data.
 * @param ts_ms       Scan timestamp (Unix ms), or NRF_CLOUD_NO_TIMESTAMP to omit.
 * @param confirmable Whether to use a confirmable CoAP transfer.
 *
 * @return 0 on success, negative error code on failure.
 */
int cloud_location_request_send(const struct location_cloud_request_data *request,
				int64_t ts_ms, bool confirmable);

#if defined(CONFIG_NRF_CLOUD_AGNSS)
/**
 * @brief Cache an A-GNSS request for later processing.
 *
 * Use this when an A-GNSS request arrives while the cloud module is not
 * in connected-ready state. The cached request will be processed when
 * cloud_location_agnss_process_cached() is called.
 *
 * @param msg Pointer to the location message containing the A-GNSS request.
 */
void cloud_location_agnss_cache(const struct location_msg *msg);

/**
 * @brief Process a previously cached A-GNSS request.
 *
 * Should be called when the cloud module enters connected-ready state.
 * If no request is cached, this function does nothing.
 */
void cloud_location_agnss_process_cached(void);
#endif /* CONFIG_NRF_CLOUD_AGNSS */

#ifdef __cplusplus
}
#endif

#endif /* _CLOUD_LOCATION_H_ */
