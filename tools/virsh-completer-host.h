/*
 * virsh-completer-host.h: virsh completer callbacks related to host
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vsh.h"

char ** virshAllocpagesPagesizeCompleter(vshControl *ctl,
                                         const vshCmd *cmd,
                                         unsigned int flags);

char ** virshCellnoCompleter(vshControl *ctl,
                             const vshCmd *cmd,
                             unsigned int flags);
