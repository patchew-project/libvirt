/*
 * virsh-pool.h: Commands to manage storage pool
 *
 * Copyright (C) 2005, 2007-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virsh.h"

virStoragePoolPtr
virshCommandOptPoolBy(vshControl *ctl, const vshCmd *cmd, const char *optname,
                      const char **name, unsigned int flags);

/* default is lookup by Name and UUID */
#define virshCommandOptPool(_ctl, _cmd, _optname, _name) \
    virshCommandOptPoolBy(_ctl, _cmd, _optname, _name, \
                          VIRSH_BYUUID | VIRSH_BYNAME)

struct virshPoolEventCallback {
    const char *name;
    virConnectStoragePoolEventGenericCallback cb;
};
typedef struct virshPoolEventCallback virshPoolEventCallback;

extern virshPoolEventCallback virshPoolEventCallbacks[];

extern const vshCmdDef storagePoolCmds[];
