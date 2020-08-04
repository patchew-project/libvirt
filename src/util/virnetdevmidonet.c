/*
 * Copyright (C) 2015 Midokura, Sarl.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "virnetdevmidonet.h"
#include "vircommand.h"
#include "viralloc.h"
#include "virerror.h"
#include "viruuid.h"

#define VIR_FROM_THIS VIR_FROM_NONE

/**
 * virNetDevMidonetBindPort:
 * @ifname: the network interface name
 * @virtualport: the midonet specific fields
 *
 * Bind an interface to a Midonet virtual port
 *
 * Returns 0 in case of success or -1 in case of failure.
 */
int
virNetDevMidonetBindPort(const char *ifname,
                         const virNetDevVPortProfile *virtualport)
{
    int ret = -1;
    virCommandPtr cmd = NULL;
    char virtportuuid[VIR_UUID_STRING_BUFLEN];

    virUUIDFormat(virtualport->interfaceID, virtportuuid);

    cmd = virCommandNew(MM_CTL);

    virCommandAddArgList(cmd, "--bind-port", virtportuuid, ifname, NULL);

    if (virCommandRun(cmd, NULL) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unable to bind port %s to the virtual port %s"),
                       ifname, virtportuuid);
        goto cleanup;
    }

    ret = 0;
 cleanup:
    virCommandFree(cmd);
    return ret;
}

/**
 * virNetDevMidonetUnbindPort:
 * @virtualport: the midonet specific fields
 *
 * Unbinds a virtual port from the host
 *
 * Returns 0 in case of success or -1 in case of failure.
 */
int
virNetDevMidonetUnbindPort(const virNetDevVPortProfile *virtualport)
{
    int ret = -1;
    virCommandPtr cmd = NULL;
    char virtportuuid[VIR_UUID_STRING_BUFLEN];

    virUUIDFormat(virtualport->interfaceID, virtportuuid);

    cmd = virCommandNew(MM_CTL);
    virCommandAddArgList(cmd, "--unbind-port", virtportuuid, NULL);

    if (virCommandRun(cmd, NULL) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unable to unbind the virtual port %s from Midonet"),
                       virtportuuid);
        goto cleanup;
    }

    ret = 0;
 cleanup:
    virCommandFree(cmd);
    return ret;
}
