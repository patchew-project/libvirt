/*
 * virnetclientstream.h: generic network RPC client stream
 *
 * Copyright (C) 2006-2011 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virnetclientprogram.h"
#include "virobject.h"

typedef struct _virNetClientStream virNetClientStream;
typedef virNetClientStream *virNetClientStreamPtr;

typedef enum {
    VIR_NET_CLIENT_STREAM_CLOSED_NOT = 0,
    VIR_NET_CLIENT_STREAM_CLOSED_FINISHED,
    VIR_NET_CLIENT_STREAM_CLOSED_ABORTED,
} virNetClientStreamClosed;

typedef void (*virNetClientStreamEventCallback)(virNetClientStreamPtr stream,
                                                int events, void *opaque);

virNetClientStreamPtr virNetClientStreamNew(virNetClientProgramPtr prog,
                                            int proc,
                                            unsigned serial,
                                            bool allowSkip);

int virNetClientStreamCheckState(virNetClientStreamPtr st);

int virNetClientStreamCheckSendStatus(virNetClientStreamPtr st,
                                      virNetMessagePtr msg);

int virNetClientStreamSetError(virNetClientStreamPtr st,
                               virNetMessagePtr msg);

void virNetClientStreamSetClosed(virNetClientStreamPtr st,
                                 virNetClientStreamClosed closed);

bool virNetClientStreamMatches(virNetClientStreamPtr st,
                               virNetMessagePtr msg);

int virNetClientStreamQueuePacket(virNetClientStreamPtr st,
                                  virNetMessagePtr msg);

int virNetClientStreamSendPacket(virNetClientStreamPtr st,
                                 virNetClientPtr client,
                                 int status,
                                 const char *data,
                                 size_t nbytes);

int virNetClientStreamRecvPacket(virNetClientStreamPtr st,
                                 virNetClientPtr client,
                                 char *data,
                                 size_t nbytes,
                                 bool nonblock,
                                 unsigned int flags);

int virNetClientStreamSendHole(virNetClientStreamPtr st,
                               virNetClientPtr client,
                               long long length,
                               unsigned int flags);

int virNetClientStreamRecvHole(virNetClientPtr client,
                               virNetClientStreamPtr st,
                               long long *length);

int virNetClientStreamEventAddCallback(virNetClientStreamPtr st,
                                       int events,
                                       virNetClientStreamEventCallback cb,
                                       void *opaque,
                                       virFreeCallback ff);

int virNetClientStreamEventUpdateCallback(virNetClientStreamPtr st,
                                          int events);
int virNetClientStreamEventRemoveCallback(virNetClientStreamPtr st);

bool virNetClientStreamEOF(virNetClientStreamPtr st)
    ATTRIBUTE_NONNULL(1);
