/*
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#ifdef __linux__
# include "internal.h"
# include "virstring.h"
# include "virnetdev.h"

# define NET_DEV_TEST_DATA_PREFIX abs_srcdir "/virnetdevtestdata/sys/class/net"

int
virNetDevSysfsFile(char **pf_sysfs_device_link,
                   const char *ifname,
                   const char *file)
{
    *pf_sysfs_device_link = g_strdup_printf("%s/%s/%s",
                                            NET_DEV_TEST_DATA_PREFIX, ifname, file);
    return 0;
}
#else
/* Nothing to override on non-__linux__ platforms */
#endif
