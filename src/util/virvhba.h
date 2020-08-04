/*
 * virvhba.h: Generic vHBA management utility functions
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

enum {
    VPORT_CREATE,
    VPORT_DELETE,
};

bool
virVHBAPathExists(const char *sysfs_prefix, int host);

bool
virVHBAIsVportCapable(const char *sysfs_prefix, int host);

char *
virVHBAGetConfig(const char *sysfs_prefix,
                 int host,
                 const char *entry)
    ATTRIBUTE_NONNULL(3);

char *
virVHBAFindVportHost(const char *sysfs_prefix);

int
virVHBAManageVport(const int parent_host,
                   const char *wwpn,
                   const char *wwnn,
                   int operation)
    ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3);

char *
virVHBAGetHostByWWN(const char *sysfs_prefix,
                    const char *wwnn,
                    const char *wwpn)
    ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3);

char *
virVHBAGetHostByFabricWWN(const char *sysfs_prefix,
                          const char *fabric_wwn)
    ATTRIBUTE_NONNULL(2);
