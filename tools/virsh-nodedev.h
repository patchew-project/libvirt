/*
 * virsh-nodedev.h: Commands in node device group
 *
 * Copyright (C) 2005, 2007-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virsh.h"

struct virshNodeDeviceEventCallback {
    const char *name;
    virConnectNodeDeviceEventGenericCallback cb;
};
typedef struct virshNodeDeviceEventCallback virshNodeDeviceEventCallback;

extern virshNodeDeviceEventCallback virshNodeDeviceEventCallbacks[];

extern const vshCmdDef nodedevCmds[];
