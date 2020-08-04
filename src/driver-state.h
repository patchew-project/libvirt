/*
 * driver-state.h: entry points for state drivers
 *
 * Copyright (C) 2006-2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#ifndef __VIR_DRIVER_H_INCLUDES___
# error "Don't include this file directly, only use driver.h"
#endif

typedef enum {
    VIR_DRV_STATE_INIT_ERROR = -1,
    VIR_DRV_STATE_INIT_SKIPPED,
    VIR_DRV_STATE_INIT_COMPLETE,
} virDrvStateInitResult;

typedef virDrvStateInitResult
(*virDrvStateInitialize)(bool privileged,
                         const char *root,
                         virStateInhibitCallback callback,
                         void *opaque);

typedef int
(*virDrvStateCleanup)(void);

typedef int
(*virDrvStateReload)(void);

typedef int
(*virDrvStateStop)(void);

typedef struct _virStateDriver virStateDriver;
typedef virStateDriver *virStateDriverPtr;

struct _virStateDriver {
    const char *name;
    bool initialized;
    virDrvStateInitialize stateInitialize;
    virDrvStateCleanup stateCleanup;
    virDrvStateReload stateReload;
    virDrvStateStop stateStop;
};
