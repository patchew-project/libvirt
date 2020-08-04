/*
 * virsh-completer.h: virsh completer callbacks
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virsh-completer-domain.h"
#include "virsh-completer-host.h"
#include "virsh-completer-interface.h"
#include "virsh-completer-network.h"
#include "virsh-completer-nodedev.h"
#include "virsh-completer-nwfilter.h"
#include "virsh-completer-pool.h"
#include "virsh-completer-secret.h"
#include "virsh-completer-snapshot.h"
#include "virsh-completer-volume.h"

char ** virshCommaStringListComplete(const char *input,
                                     const char **options);

char ** virshCheckpointNameCompleter(vshControl *ctl,
                                     const vshCmd *cmd,
                                     unsigned int flags);
