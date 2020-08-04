/*
 * virscsihost.h: Generic scsi_host management utility functions
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

int virSCSIHostGetUniqueId(const char *sysfs_prefix, int host);

char *virSCSIHostFindByPCI(const char *sysfs_prefix,
                           const char *parentaddr,
                           unsigned int unique_id);

int virSCSIHostGetNumber(const char *adapter_name,
                         unsigned int *result)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

char *virSCSIHostGetNameByParentaddr(unsigned int domain,
                                     unsigned int bus,
                                     unsigned int slot,
                                     unsigned int function,
                                     unsigned int unique_id);
