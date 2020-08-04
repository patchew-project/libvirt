/*
 * virsh-completer-secret.c: virsh completer callbacks related to secret
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "virsh-completer-secret.h"
#include "viralloc.h"
#include "virsh-secret.h"
#include "virsh-util.h"
#include "virsh.h"
#include "virstring.h"

char **
virshSecretUUIDCompleter(vshControl *ctl,
                         const vshCmd *cmd G_GNUC_UNUSED,
                         unsigned int flags)
{
    virshControlPtr priv = ctl->privData;
    virSecretPtr *secrets = NULL;
    int nsecrets = 0;
    size_t i = 0;
    char **ret = NULL;
    VIR_AUTOSTRINGLIST tmp = NULL;

    virCheckFlags(0, NULL);

    if (!priv->conn || virConnectIsAlive(priv->conn) <= 0)
        return NULL;

    if ((nsecrets = virConnectListAllSecrets(priv->conn, &secrets, flags)) < 0)
        return NULL;

    if (VIR_ALLOC_N(tmp, nsecrets + 1) < 0)
        goto cleanup;

    for (i = 0; i < nsecrets; i++) {
        char uuid[VIR_UUID_STRING_BUFLEN];

        if (virSecretGetUUIDString(secrets[i], uuid) < 0)
            goto cleanup;
        tmp[i] = g_strdup(uuid);
    }

    ret = g_steal_pointer(&tmp);

 cleanup:
    for (i = 0; i < nsecrets; i++)
        virshSecretFree(secrets[i]);
    VIR_FREE(secrets);
    return ret;
}


char **
virshSecretEventNameCompleter(vshControl *ctl G_GNUC_UNUSED,
                              const vshCmd *cmd G_GNUC_UNUSED,
                              unsigned int flags)
{
    size_t i;
    VIR_AUTOSTRINGLIST tmp = NULL;

    virCheckFlags(0, NULL);

    if (VIR_ALLOC_N(tmp, VIR_SECRET_EVENT_ID_LAST + 1) < 0)
        return NULL;

    for (i = 0; i < VIR_SECRET_EVENT_ID_LAST; i++)
        tmp[i] = g_strdup(virshSecretEventCallbacks[i].name);

    return g_steal_pointer(&tmp);
}
