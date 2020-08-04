/*
 * virsh-nwfilter.h: Commands to manage network filters
 *
 * Copyright (C) 2005, 2007-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virsh.h"

virNWFilterPtr
virshCommandOptNWFilterBy(vshControl *ctl, const vshCmd *cmd,
                          const char **name, unsigned int flags);

virNWFilterBindingPtr
virshCommandOptNWFilterBindingBy(vshControl *ctl, const vshCmd *cmd,
                                 const char **name, unsigned int flags);

/* default is lookup by Name and UUID */
#define virshCommandOptNWFilter(_ctl, _cmd, _name) \
    virshCommandOptNWFilterBy(_ctl, _cmd, _name, \
                              VIRSH_BYUUID | VIRSH_BYNAME)

/* default is lookup by port dev */
#define virshCommandOptNWFilterBinding(_ctl, _cmd, _name) \
    virshCommandOptNWFilterBindingBy(_ctl, _cmd, _name, 0)

extern const vshCmdDef nwfilterCmds[];
