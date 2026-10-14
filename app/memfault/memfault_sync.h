/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _MEMFAULT_SYNC_H_
#define _MEMFAULT_SYNC_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Queue a Memfault heartbeat (collect and serialize all heartbeat metrics).
 *
 * Same as the "mflt heartbeat" shell command. Runs on the Memfault sync workqueue, so it does
 * not block the caller. A heartbeat queued before an upload is always serialized first.
 */
void memfault_sync_heartbeat_trigger(void);

/**
 * @brief Queue an upload of all pending Memfault data.
 *
 * Same as the "mflt post_chunks" shell command. Runs on the Memfault sync workqueue. Only call
 * while connected to the cloud; the Memfault CoAP transport otherwise opens its own connection.
 */
void memfault_sync_upload_trigger(void);

#ifdef __cplusplus
}
#endif

#endif /* _MEMFAULT_SYNC_H_ */
