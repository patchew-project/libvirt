/*
 * virsh-secret.h: Commands to manage secret
 *
 * Copyright (C) 2005, 2007-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vsh.h"

struct virshSecretEventCallback {
    const char *name;
    virConnectSecretEventGenericCallback cb;
};
typedef struct virshSecretEventCallback virshSecretEventCallback;

extern virshSecretEventCallback virshSecretEventCallbacks[];

extern const vshCmdDef secretCmds[];
