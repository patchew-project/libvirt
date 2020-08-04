/*
 * virfcp.c: Utility functions for the Fibre Channel Protocol
 *
 * Copyright (C) 2017 IBM Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "internal.h"

#include "viralloc.h"
#include "virfile.h"
#include "virerror.h"
#include "virstring.h"

#include "virfcp.h"

#define VIR_FROM_THIS VIR_FROM_NONE

#ifdef __linux__

# define SYSFS_FC_RPORT_PATH "/sys/class/fc_remote_ports"

bool
virFCIsCapableRport(const char *rport)
{
    g_autofree char *path = NULL;

    if (virBuildPath(&path, SYSFS_FC_RPORT_PATH, rport) < 0)
        return false;

    return virFileExists(path);
}

int
virFCReadRportValue(const char *rport,
                    const char *entry,
                    char **result)
{
    g_autofree char *buf = NULL;
    char *p = NULL;

    if (virFileReadValueString(&buf, "%s/%s/%s",
                               SYSFS_FC_RPORT_PATH, rport, entry) < 0) {
        return -1;
    }

    if ((p = strchr(buf, '\n')))
        *p = '\0';

    *result = g_strdup(buf);

    return 0;
}

#else

bool
virFCIsCapableRport(const char *rport G_GNUC_UNUSED)
{
    virReportSystemError(ENOSYS, "%s", _("Not supported on this platform"));
    return false;
}

int
virFCReadRportValue(const char *rport G_GNUC_UNUSED,
                    const char *entry G_GNUC_UNUSED,
                    char **result G_GNUC_UNUSED)
{
    virReportSystemError(ENOSYS, "%s", _("Not supported on this platform"));
    return -1;
}

#endif
