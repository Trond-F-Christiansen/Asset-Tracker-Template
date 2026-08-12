/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Manual PPR launcher: replicates drivers/misc/nordic_vpr_launcher behaviour
 * but driven from a shell command instead of a POST_KERNEL SYS_INIT hook, so
 * we can decide at runtime when to bring the PPR up relative to other init
 * (specifically: nrf_modem_lib_init -> ironside_se_cpuconf(CELLCORE) and the
 * cpuapp<->cpucell ICMSG handshake).
 *
 * Pairs with overlay-ppr-manual.overlay (use EXTRA_DTC_OVERLAY_FILE so board
 * overlays are not skipped), which removes the VPR phandles from &cpuppr_vpr so
 * the upstream launcher's init() early-returns.
 */

#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <hal/nrf_vpr.h>

LOG_MODULE_REGISTER(ppr_launcher, CONFIG_APP_PPR_LAUNCHER_LOG_LEVEL);

#define VPR_NODE	DT_NODELABEL(cpuppr_vpr)
#define EXEC_NODE	DT_NODELABEL(cpuppr_code_data)
#define SRC_NODE	DT_NODELABEL(cpuppr_code_partition)

BUILD_ASSERT(DT_NODE_HAS_STATUS(VPR_NODE, okay),
	     "cpuppr_vpr must be enabled (use the nordic-ppr snippet)");
BUILD_ASSERT(DT_NODE_EXISTS(EXEC_NODE), "cpuppr_code_data node missing");
BUILD_ASSERT(DT_NODE_EXISTS(SRC_NODE), "cpuppr_code_partition node missing");
BUILD_ASSERT(DT_REG_SIZE(EXEC_NODE) <= DT_REG_SIZE(SRC_NODE),
	     "cpuppr_code_data exec region exceeds source partition");

static NRF_VPR_Type *const vpr_reg = (NRF_VPR_Type *)DT_REG_ADDR(VPR_NODE);

static int ppr_load_and_run(void)
{
	uintptr_t exec_addr = DT_REG_ADDR(EXEC_NODE);
	uintptr_t src_addr = DT_REG_ADDR(SRC_NODE);
	size_t size = DT_REG_SIZE(EXEC_NODE);

	if (nrf_vpr_cpurun_get(vpr_reg)) {
		LOG_WRN("PPR already running");

		return -EALREADY;
	}

	LOG_INF("Loading VPR (%p) from %p to %p (%zu bytes)",
		(void *)vpr_reg, (void *)src_addr, (void *)exec_addr, size);

	memcpy((void *)exec_addr, (void *)src_addr, size);

#if defined(CONFIG_DCACHE)
	LOG_INF("Writing back cache with loaded VPR (from %p %zu bytes)",
		(void *)exec_addr, size);
	sys_cache_data_flush_range((void *)exec_addr, size);
#endif

	LOG_INF("Launching VPR (%p) from %p", (void *)vpr_reg, (void *)exec_addr);
	nrf_vpr_initpc_set(vpr_reg, exec_addr);
	nrf_vpr_cpurun_set(vpr_reg, true);

	return 0;
}

static int ppr_halt(void)
{
	if (!nrf_vpr_cpurun_get(vpr_reg)) {
		LOG_WRN("PPR not running");

		return -EALREADY;
	}

	nrf_vpr_cpurun_set(vpr_reg, false);
	LOG_INF("PPR halted");

	return 0;
}

static int cmd_ppr_start(const struct shell *sh, size_t argc, char **argv)
{
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	err = ppr_load_and_run();
	if (err == -EALREADY) {
		shell_warn(sh, "PPR already running");

		return 0;
	}

	if (err) {
		shell_error(sh, "ppr start failed: %d", err);

		return err;
	}

	shell_print(sh, "PPR started");

	return 0;
}

static int cmd_ppr_stop(const struct shell *sh, size_t argc, char **argv)
{
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	err = ppr_halt();
	if (err == -EALREADY) {
		shell_warn(sh, "PPR not running");

		return 0;
	}

	if (err) {
		shell_error(sh, "ppr stop failed: %d", err);

		return err;
	}

	shell_print(sh, "PPR halted");

	return 0;
}

static int cmd_ppr_status(const struct shell *sh, size_t argc, char **argv)
{
	bool running;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	running = nrf_vpr_cpurun_get(vpr_reg);

	shell_print(sh, "PPR (%p): %s, INITPC=0x%08x",
		    (void *)vpr_reg,
		    running ? "running" : "halted",
		    nrf_vpr_initpc_get(vpr_reg));

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_ppr,
	SHELL_CMD(start,  NULL, "Load PPR firmware to RAM and start the core",
		  cmd_ppr_start),
	SHELL_CMD(stop,   NULL, "Halt the PPR core",
		  cmd_ppr_stop),
	SHELL_CMD(status, NULL, "Show PPR run state",
		  cmd_ppr_status),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(ppr, &sub_ppr, "PPR launcher control", NULL);
