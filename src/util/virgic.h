/*
 * virgic.h: ARM Generic Interrupt Controller support
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virenum.h"

typedef enum {
    VIR_GIC_VERSION_NONE = 0,
    VIR_GIC_VERSION_HOST,
    VIR_GIC_VERSION_2,
    VIR_GIC_VERSION_3,
    VIR_GIC_VERSION_LAST
} virGICVersion;

VIR_ENUM_DECL(virGICVersion);

typedef enum {
    VIR_GIC_IMPLEMENTATION_NONE = 0,
    VIR_GIC_IMPLEMENTATION_KERNEL = (1 << 1),
    VIR_GIC_IMPLEMENTATION_EMULATED = (1 << 2)
} virGICImplementation;

typedef struct _virGICCapability virGICCapability;
typedef virGICCapability *virGICCapabilityPtr;
struct _virGICCapability {
    virGICVersion version;
    virGICImplementation implementation;
};
