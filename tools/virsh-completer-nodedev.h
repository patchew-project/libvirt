/*
 * virsh-completer-nodedev.h: virsh completer callbacks related to nodedev
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vsh.h"

char ** virshNodeDeviceNameCompleter(vshControl *ctl,
                                     const vshCmd *cmd,
                                     unsigned int flags);

char ** virshNodeDeviceEventNameCompleter(vshControl *ctl,
                                          const vshCmd *cmd,
                                          unsigned int flags);

char ** virshNodeDeviceCapabilityNameCompleter(vshControl *ctl,
                                               const vshCmd *cmd,
                                               unsigned int flags);
