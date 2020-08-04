/*
 * virsh-completer-snapshot.h: virsh completer callbacks related to snapshots
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vsh.h"

char ** virshSnapshotNameCompleter(vshControl *ctl,
                                   const vshCmd *cmd,
                                   unsigned int flags);
