/*
 * virsh-completer-volume.c: virsh completer callbacks related to volumes
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "virsh-completer-volume.h"
#include "viralloc.h"
#include "virsh-pool.h"
#include "virsh.h"
#include "virstring.h"

char **
virshStorageVolNameCompleter(vshControl *ctl,
                             const vshCmd *cmd,
                             unsigned int flags)
{
    virshControlPtr priv = ctl->privData;
    virStoragePoolPtr pool = NULL;
    virStorageVolPtr *vols = NULL;
    int rc;
    int nvols = 0;
    size_t i = 0;
    char **ret = NULL;
    VIR_AUTOSTRINGLIST tmp = NULL;

    virCheckFlags(0, NULL);

    if (!priv->conn || virConnectIsAlive(priv->conn) <= 0)
        return NULL;

    if (!(pool = virshCommandOptPool(ctl, cmd, "pool", NULL)))
        return NULL;

    if ((rc = virStoragePoolListAllVolumes(pool, &vols, flags)) < 0)
        goto cleanup;
    nvols = rc;

    if (VIR_ALLOC_N(tmp, nvols + 1) < 0)
        goto cleanup;

    for (i = 0; i < nvols; i++) {
        const char *name = virStorageVolGetName(vols[i]);

        tmp[i] = g_strdup(name);
    }

    ret = g_steal_pointer(&tmp);

 cleanup:
    virStoragePoolFree(pool);
    for (i = 0; i < nvols; i++)
        virStorageVolFree(vols[i]);
    VIR_FREE(vols);
    return ret;
}
