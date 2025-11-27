/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _APP_COMMON_H_
#define _APP_COMMON_H_

#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/util.h>  /* For Zephyr's utility macros, including MAX */
#if defined(CONFIG_MEMFAULT)
#include <memfault/panics/assert.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Handle fatal error.
 *  @param is_watchdog_timeout Boolean indicating if the macro was called upon a watchdog timeout.
 */
#define FATAL_ERROR_HANDLE(is_watchdog_timeout) do {				\
	LOG_PANIC();								\
	if (is_watchdog_timeout) {						\
		IF_ENABLED(CONFIG_MEMFAULT, (MEMFAULT_SOFTWARE_WATCHDOG()));	\
	}									\
	k_sleep(K_SECONDS(10));							\
	__ASSERT(false, "SEND_FATAL_ERROR() macro called");			\
} while (0)

/** @brief Macro used to handle fatal errors. */
#define SEND_FATAL_ERROR() FATAL_ERROR_HANDLE(0)

/** @brief Macro used to handle watchdog timeouts. */
#define SEND_FATAL_ERROR_WATCHDOG_TIMEOUT() FATAL_ERROR_HANDLE(1)

/* Helper macro to create union member from channel and type */
#define UNION_MEMBER(_chan, _type) _type _chan##_data_type;


/**
 * @brief Helper macros to create union members from Nth argument
 */
#define UNION_MEMBER_ARG_1(...) GET_ARG_N(1, __VA_ARGS__) CONCAT(member_, __COUNTER__);
#define UNION_MEMBER_ARG_2(...) GET_ARG_N(2, __VA_ARGS__) CONCAT(member_, __COUNTER__);
#define UNION_MEMBER_ARG_3(...) GET_ARG_N(3, __VA_ARGS__) CONCAT(member_, __COUNTER__);
#define UNION_MEMBER_ARG_4(...) GET_ARG_N(4, __VA_ARGS__) CONCAT(member_, __COUNTER__);

/**
 * @brief Macro to compute the maximum type size of a specific element from a X-macro list.
 *
 * @param _LIST List of elements to compute the maximum type size from.
 * @param index Index of the element in the list to use for size calculation.
 *
 * @return Maximum type size of the specified element from the list.
 */
#define MAX_TYPE_SIZE_FROM_LIST_ELEMENT(_LIST, index) \
        sizeof(union {_LIST(UNION_MEMBER_ARG_##index)})

#ifdef __cplusplus
}
#endif

#endif /* _APP_COMMON_H_ */
