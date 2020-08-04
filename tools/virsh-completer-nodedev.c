/*
 * virsh-completer-nodedev.c: virsh completer callbacks related to nodedev
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "virsh-completer-nodedev.h"
#include "conf/node_device_conf.h"
#include "viralloc.h"
#include "virsh-nodedev.h"
#include "virsh.h"
#include "virstring.h"

char **
virshNodeDeviceNameCompleter(vshControl *ctl,
                             const vshCmd *cmd G_GNUC_UNUSED,
                             unsigned int flags)
{
    virshControlPtr priv = ctl->privData;
    virNodeDevicePtr *devs = NULL;
    int ndevs = 0;
    size_t i = 0;
    char **ret = NULL;
    VIR_AUTOSTRINGLIST tmp = NULL;

    virCheckFlags(0, NULL);

    if (!priv->conn || virConnectIsAlive(priv->conn) <= 0)
        return NULL;

    if ((ndevs = virConnectListAllNodeDevices(priv->conn, &devs, flags)) < 0)
        return NULL;

    if (VIR_ALLOC_N(tmp, ndevs + 1) < 0)
        goto cleanup;

    for (i = 0; i < ndevs; i++) {
        const char *name = virNodeDeviceGetName(devs[i]);

        tmp[i] = g_strdup(name);
    }

    ret = g_steal_pointer(&tmp);

 cleanup:
    for (i = 0; i < ndevs; i++)
        virNodeDeviceFree(devs[i]);
    VIR_FREE(devs);
    return ret;
}


char **
virshNodeDeviceEventNameCompleter(vshControl *ctl G_GNUC_UNUSED,
                                  const vshCmd *cmd G_GNUC_UNUSED,
                                  unsigned int flags)
{
    size_t i = 0;
    VIR_AUTOSTRINGLIST tmp = NULL;

    virCheckFlags(0, NULL);

    if (VIR_ALLOC_N(tmp, VIR_NODE_DEVICE_EVENT_ID_LAST + 1) < 0)
        return NULL;

    for (i = 0; i < VIR_NODE_DEVICE_EVENT_ID_LAST; i++)
        tmp[i] = g_strdup(virshNodeDeviceEventCallbacks[i].name);

    return g_steal_pointer(&tmp);
}


char **
virshNodeDeviceCapabilityNameCompleter(vshControl *ctl,
                                       const vshCmd *cmd,
                                       unsigned int flags)
{
    VIR_AUTOSTRINGLIST tmp = NULL;
    const char *cap_str = NULL;
    size_t i = 0;

    virCheckFlags(0, NULL);

    if (vshCommandOptStringQuiet(ctl, cmd, "cap", &cap_str) < 0)
        return NULL;

    if (VIR_ALLOC_N(tmp, VIR_NODE_DEV_CAP_LAST + 1) < 0)
        return NULL;

    for (i = 0; i < VIR_NODE_DEV_CAP_LAST; i++)
        tmp[i] = g_strdup(virNodeDevCapTypeToString(i));

    return virshCommaStringListComplete(cap_str, (const char **)tmp);
}
