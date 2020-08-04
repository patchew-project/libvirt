/*
 * driver-stream.h: entry points for stream drivers
 *
 * Copyright (C) 2006-2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#ifndef __VIR_DRIVER_H_INCLUDES___
# error "Don't include this file directly, only use driver.h"
#endif

typedef int
(*virDrvStreamSend)(virStreamPtr st,
                    const char *data,
                    size_t nbytes);

typedef int
(*virDrvStreamRecv)(virStreamPtr st,
                    char *data,
                    size_t nbytes);

typedef int
(*virDrvStreamRecvFlags)(virStreamPtr st,
                         char *data,
                         size_t nbytes,
                         unsigned int flags);

typedef int
(*virDrvStreamSendHole)(virStreamPtr st,
                        long long length,
                        unsigned int flags);

typedef int
(*virDrvStreamRecvHole)(virStreamPtr st,
                        long long *length,
                        unsigned int flags);

typedef int
(*virDrvStreamInData)(virStreamPtr st,
                      int *data,
                      long long *length);

typedef int
(*virDrvStreamEventAddCallback)(virStreamPtr stream,
                                int events,
                                virStreamEventCallback cb,
                                void *opaque,
                                virFreeCallback ff);

typedef int
(*virDrvStreamEventUpdateCallback)(virStreamPtr stream,
                                   int events);

typedef int
(*virDrvStreamEventRemoveCallback)(virStreamPtr stream);

typedef int
(*virDrvStreamFinish)(virStreamPtr st);

typedef int
(*virDrvStreamAbort)(virStreamPtr st);

typedef struct _virStreamDriver virStreamDriver;
typedef virStreamDriver *virStreamDriverPtr;

struct _virStreamDriver {
    virDrvStreamSend streamSend;
    virDrvStreamRecv streamRecv;
    virDrvStreamRecvFlags streamRecvFlags;
    virDrvStreamSendHole streamSendHole;
    virDrvStreamRecvHole streamRecvHole;
    virDrvStreamInData streamInData;
    virDrvStreamEventAddCallback streamEventAddCallback;
    virDrvStreamEventUpdateCallback streamEventUpdateCallback;
    virDrvStreamEventRemoveCallback streamEventRemoveCallback;
    virDrvStreamFinish streamFinish;
    virDrvStreamAbort streamAbort;
};
