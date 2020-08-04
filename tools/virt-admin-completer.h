/*
 * virt-admin-completer.h: virt-admin completer callbacks
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vsh.h"

char **
vshAdmServerCompleter(vshControl *ctl,
                      const vshCmd *cmd,
                      unsigned int flags);
