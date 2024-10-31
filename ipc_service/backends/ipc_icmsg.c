/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <../lib/icmsg.h>

#include <zephyr/ipc/ipc_service_backend.h>

#include "ipc_icmsg.h"

#define DT_DRV_COMPAT	zephyr_ipc_icmsg


#define DEFINE_BACKEND_DEVICE(i)

DT_INST_FOREACH_STATUS_OKAY(DEFINE_BACKEND_DEVICE)
