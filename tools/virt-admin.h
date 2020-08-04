/*
 * virt-admin.h: a shell to exercise the libvirt admin API
 *
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "vsh.h"

#define VIR_FROM_THIS VIR_FROM_NONE

/*
 * Command group types
 */

typedef struct _vshAdmControl vshAdmControl;
typedef vshAdmControl *vshAdmControlPtr;

/*
 * adminControl
 */
struct _vshAdmControl {
    virAdmConnectPtr conn;      /* connection to a daemon's admin server */
    bool wantReconnect;
};
