/*
 * virsh-interface.h: Commands to manage host interface
 *
 * Copyright (C) 2005, 2007-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virsh.h"

virInterfacePtr virshCommandOptInterfaceBy(vshControl *ctl, const vshCmd *cmd,
                                           const char *optname,
                                           const char **name, unsigned int flags);

/* default is lookup by Name and MAC */
#define virshCommandOptInterface(_ctl, _cmd, _name) \
    virshCommandOptInterfaceBy(_ctl, _cmd, NULL, _name, \
                               VIRSH_BYMAC | VIRSH_BYNAME)

extern const vshCmdDef ifaceCmds[];
