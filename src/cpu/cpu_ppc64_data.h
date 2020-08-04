/*
 * cpu_ppc64_data.h: 64-bit PowerPC CPU specific data
 *
 * Copyright (C) 2012 IBM Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

typedef struct _virCPUppc64PVR virCPUppc64PVR;
struct _virCPUppc64PVR {
    uint32_t value;
    uint32_t mask;
};

#define VIR_CPU_PPC64_DATA_INIT { 0 }

typedef struct _virCPUppc64Data virCPUppc64Data;
struct _virCPUppc64Data {
    size_t len;
    virCPUppc64PVR *pvr;
};
