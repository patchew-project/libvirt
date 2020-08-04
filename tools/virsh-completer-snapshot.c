/*
 * virsh-completer-snapshot.c: virsh completer callbacks related to snapshots
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "virsh-completer-snapshot.h"
#include "viralloc.h"
#include "virsh-util.h"
#include "virsh.h"
#include "virstring.h"

char **
virshSnapshotNameCompleter(vshControl *ctl,
                           const vshCmd *cmd,
                           unsigned int flags)
{
    virshControlPtr priv = ctl->privData;
    virDomainPtr dom = NULL;
    virDomainSnapshotPtr *snapshots = NULL;
    int rc;
    int nsnapshots = 0;
    size_t i = 0;
    char **ret = NULL;
    VIR_AUTOSTRINGLIST tmp = NULL;

    virCheckFlags(0, NULL);

    if (!priv->conn || virConnectIsAlive(priv->conn) <= 0)
        return NULL;

    if (!(dom = virshCommandOptDomain(ctl, cmd, NULL)))
        return NULL;

    if ((rc = virDomainListAllSnapshots(dom, &snapshots, flags)) < 0)
        goto cleanup;
    nsnapshots = rc;

    if (VIR_ALLOC_N(tmp, nsnapshots + 1) < 0)
        goto cleanup;

    for (i = 0; i < nsnapshots; i++) {
        const char *name = virDomainSnapshotGetName(snapshots[i]);

        tmp[i] = g_strdup(name);
    }

    ret = g_steal_pointer(&tmp);

 cleanup:
    virshDomainFree(dom);
    for (i = 0; i < nsnapshots; i++)
        virshDomainSnapshotFree(snapshots[i]);
    VIR_FREE(snapshots);
    return ret;
}
