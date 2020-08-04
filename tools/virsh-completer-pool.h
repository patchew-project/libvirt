/*
 * virsh-completer-pool.h: virsh completer callbacks related to pools
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vsh.h"

char ** virshStoragePoolNameCompleter(vshControl *ctl,
                                      const vshCmd *cmd,
                                      unsigned int flags);

char ** virshPoolEventNameCompleter(vshControl *ctl,
                                    const vshCmd *cmd,
                                    unsigned int flags);

char ** virshPoolTypeCompleter(vshControl *ctl,
                               const vshCmd *cmd,
                               unsigned int flags);
