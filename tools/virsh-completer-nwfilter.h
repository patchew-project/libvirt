/*
 * virsh-completer-nwfilter.h: virsh completer callbacks related to nwfilters
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vsh.h"

char ** virshNWFilterNameCompleter(vshControl *ctl,
                                   const vshCmd *cmd,
                                   unsigned int flags);

char ** virshNWFilterBindingNameCompleter(vshControl *ctl,
                                          const vshCmd *cmd,
                                          unsigned int flags);
