/*
 * virsh-completer-checkpoint.h: virsh completer callbacks related to checkpoints
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vsh.h"

char ** virshCheckpointNameCompleter(vshControl *ctl,
                                     const vshCmd *cmd,
                                     unsigned int flags);
