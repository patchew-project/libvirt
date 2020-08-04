/*
 * virsh-network.h: Commands to manage network
 *
 * Copyright (C) 2005, 2007-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virsh.h"

virNetworkPtr
virshCommandOptNetworkBy(vshControl *ctl, const vshCmd *cmd,
                         const char **name, unsigned int flags);

virNetworkPortPtr
virshCommandOptNetworkPort(vshControl *ctl, const vshCmd *cmd,
                           virNetworkPtr net,
                           const char **name);

/* default is lookup by Name and UUID */
#define virshCommandOptNetwork(_ctl, _cmd, _name) \
    virshCommandOptNetworkBy(_ctl, _cmd, _name, \
                             VIRSH_BYUUID | VIRSH_BYNAME)

struct virshNetworkEventCallback {
    const char *name;
    virConnectNetworkEventGenericCallback cb;
};
typedef struct virshNetworkEventCallback virshNetworkEventCallback;

extern virshNetworkEventCallback virshNetworkEventCallbacks[];

extern const vshCmdDef networkCmds[];
