/*
 * cpu_arm_data.h: 64-bit arm CPU specific data
 *
 * Copyright (C) 2020 Huawei Technologies Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#define VIR_CPU_ARM_DATA_INIT { 0 }

typedef struct _virCPUarmData virCPUarmData;
struct _virCPUarmData {
    unsigned long vendor_id;
    unsigned long pvr;
    char **features;
};
