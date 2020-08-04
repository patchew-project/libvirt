/*
 * virsh-completer-network.h: virsh completer callbacks related to networks
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vsh.h"

char ** virshNetworkNameCompleter(vshControl *ctl,
                                  const vshCmd *cmd,
                                  unsigned int flags);

char ** virshNetworkEventNameCompleter(vshControl *ctl,
                                       const vshCmd *cmd,
                                       unsigned int flags);

char ** virshNetworkPortUUIDCompleter(vshControl *ctl,
                                      const vshCmd *cmd,
                                      unsigned int flags);
