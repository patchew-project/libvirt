/*
 * virkeepalive.h: keepalive handling
 *
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virnetmessage.h"
#include "virobject.h"

typedef int (*virKeepAliveSendFunc)(void *client, virNetMessagePtr msg);
typedef void (*virKeepAliveDeadFunc)(void *client);
typedef void (*virKeepAliveFreeFunc)(void *client);

typedef struct _virKeepAlive virKeepAlive;
typedef virKeepAlive *virKeepAlivePtr;


virKeepAlivePtr virKeepAliveNew(int interval,
                                unsigned int count,
                                void *client,
                                virKeepAliveSendFunc sendCB,
                                virKeepAliveDeadFunc deadCB,
                                virKeepAliveFreeFunc freeCB)
                                ATTRIBUTE_NONNULL(3) ATTRIBUTE_NONNULL(4)
                                ATTRIBUTE_NONNULL(5) ATTRIBUTE_NONNULL(6);

int virKeepAliveStart(virKeepAlivePtr ka,
                      int interval,
                      unsigned int count);
void virKeepAliveStop(virKeepAlivePtr ka);

int virKeepAliveTimeout(virKeepAlivePtr ka);
bool virKeepAliveTrigger(virKeepAlivePtr ka,
                         virNetMessagePtr *msg);
bool virKeepAliveCheckMessage(virKeepAlivePtr ka,
                              virNetMessagePtr msg,
                              virNetMessagePtr *response);
