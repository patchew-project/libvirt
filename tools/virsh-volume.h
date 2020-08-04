/*
 * virsh-volume.h: Commands to manage storage volume
 *
 * Copyright (C) 2005, 2007-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virsh.h"

virStorageVolPtr virshCommandOptVolBy(vshControl *ctl, const vshCmd *cmd,
                                      const char *optname,
                                      const char *pooloptname,
                                      const char **name, unsigned int flags);

/* default is lookup by Name and UUID */
#define virshCommandOptVol(_ctl, _cmd, _optname, _pooloptname, _name) \
    virshCommandOptVolBy(_ctl, _cmd, _optname, _pooloptname, _name, \
                         VIRSH_BYUUID | VIRSH_BYNAME)

extern const vshCmdDef storageVolCmds[];
