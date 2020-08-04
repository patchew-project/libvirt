/*
 * virsh-completer-interface.c: virsh completer callbacks related to interfaces
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "virsh-completer-interface.h"
#include "viralloc.h"
#include "virsh.h"
#include "virstring.h"

char **
virshInterfaceNameCompleter(vshControl *ctl,
                            const vshCmd *cmd G_GNUC_UNUSED,
                            unsigned int flags)
{
    virshControlPtr priv = ctl->privData;
    virInterfacePtr *ifaces = NULL;
    int nifaces = 0;
    size_t i = 0;
    char **ret = NULL;
    VIR_AUTOSTRINGLIST tmp = NULL;

    virCheckFlags(VIR_CONNECT_LIST_INTERFACES_ACTIVE |
                  VIR_CONNECT_LIST_INTERFACES_INACTIVE,
                  NULL);

    if (!priv->conn || virConnectIsAlive(priv->conn) <= 0)
        return NULL;

    if ((nifaces = virConnectListAllInterfaces(priv->conn, &ifaces, flags)) < 0)
        return NULL;

    if (VIR_ALLOC_N(tmp, nifaces + 1) < 0)
        goto cleanup;

    for (i = 0; i < nifaces; i++) {
        const char *name = virInterfaceGetName(ifaces[i]);

        tmp[i] = g_strdup(name);
    }

    ret = g_steal_pointer(&tmp);

 cleanup:
    for (i = 0; i < nifaces; i++)
        virInterfaceFree(ifaces[i]);
    VIR_FREE(ifaces);
    return ret;
}
