/*
 * virsh-completer-secret.h: virsh completer callbacks related to secret
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vsh.h"

char ** virshSecretUUIDCompleter(vshControl *ctl,
                                 const vshCmd *cmd,
                                 unsigned int flags);

char ** virshSecretEventNameCompleter(vshControl *ctl,
                                      const vshCmd *cmd,
                                      unsigned int flags);
