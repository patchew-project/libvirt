/*
 * virsh-completer-volume.h: virsh completer callbacks related to volumes
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vsh.h"


char ** virshStorageVolNameCompleter(vshControl *ctl,
                                     const vshCmd *cmd,
                                     unsigned int flags);
