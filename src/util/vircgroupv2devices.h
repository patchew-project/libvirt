/*
 * vircgroupv2devices.h: methods for cgroups v2 BPF devices
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <sys/types.h>

#include "internal.h"

#include "vircgroup.h"

bool
virCgroupV2DevicesAvailable(virCgroupPtr group)
    G_GNUC_NO_INLINE;

int
virCgroupV2DevicesDetectProg(virCgroupPtr group);

int
virCgroupV2DevicesCreateProg(virCgroupPtr group);

int
virCgroupV2DevicesPrepareProg(virCgroupPtr group);

int
virCgroupV2DevicesRemoveProg(virCgroupPtr group);

uint32_t
virCgroupV2DevicesGetPerms(int perms,
                           char type);

uint64_t
virCgroupV2DevicesGetKey(int major,
                         int minor);
