/*
 * virt-admin-completer.c: virt-admin completer callbacks
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "virt-admin-completer.h"
#include "internal.h"
#include "virt-admin.h"
#include "viralloc.h"


char **
vshAdmServerCompleter(vshControl *ctl,
                      const vshCmd *cmd G_GNUC_UNUSED,
                      unsigned int flags)
{
    vshAdmControlPtr priv = ctl->privData;
    virAdmServerPtr *srvs = NULL;
    int nsrvs = 0;
    size_t i = 0;
    char **ret = NULL;

    virCheckFlags(0, NULL);

    if (!priv->conn || virAdmConnectIsAlive(priv->conn) <= 0)
        return NULL;

    /* Obtain a list of available servers on the daemon */
    if ((nsrvs = virAdmConnectListServers(priv->conn, &srvs, 0)) < 0)
        return NULL;

    if (VIR_ALLOC_N(ret, nsrvs + 1) < 0)
        goto error;

    for (i = 0; i < nsrvs; i++) {
        const char *name = virAdmServerGetName(srvs[i]);

        ret[i] = g_strdup(name);

        virAdmServerFree(srvs[i]);
    }
    VIR_FREE(srvs);

    return ret;

 error:
    for (; i < nsrvs; i++)
        virAdmServerFree(srvs[i]);
    VIR_FREE(srvs);
    for (i = 0; i < nsrvs; i++)
        VIR_FREE(ret[i]);
    VIR_FREE(ret);
    return ret;
}
