/*
 * virnetmessage.h: basic RPC message encoding/decoding
 *
 * Copyright (C) 2010-2011 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virnetprotocol.h"

typedef struct virNetMessageHeader *virNetMessageHeaderPtr;
typedef struct virNetMessageError *virNetMessageErrorPtr;

typedef struct _virNetMessage virNetMessage;
typedef virNetMessage *virNetMessagePtr;

typedef void (*virNetMessageFreeCallback)(virNetMessagePtr msg, void *opaque);

struct _virNetMessage {
    bool tracked;

    char *buffer; /* Initially VIR_NET_MESSAGE_INITIAL + VIR_NET_MESSAGE_LEN_MAX */
                  /* Maximum   VIR_NET_MESSAGE_MAX     + VIR_NET_MESSAGE_LEN_MAX */
    size_t bufferLength;
    size_t bufferOffset;

    virNetMessageHeader header;

    virNetMessageFreeCallback cb;
    void *opaque;

    size_t nfds;
    int *fds;
    size_t donefds;

    virNetMessagePtr next;
};


virNetMessagePtr virNetMessageNew(bool tracked);

void virNetMessageClearPayload(virNetMessagePtr msg);

void virNetMessageClear(virNetMessagePtr);

void virNetMessageFree(virNetMessagePtr msg);

virNetMessagePtr virNetMessageQueueServe(virNetMessagePtr *queue)
    ATTRIBUTE_NONNULL(1);
void virNetMessageQueuePush(virNetMessagePtr *queue,
                            virNetMessagePtr msg)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int virNetMessageEncodeHeader(virNetMessagePtr msg)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;
int virNetMessageDecodeLength(virNetMessagePtr msg)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;
int virNetMessageDecodeHeader(virNetMessagePtr msg)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;

int virNetMessageEncodePayload(virNetMessagePtr msg,
                               xdrproc_t filter,
                               void *data)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;
int virNetMessageDecodePayload(virNetMessagePtr msg,
                               xdrproc_t filter,
                               void *data)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;

int virNetMessageEncodeNumFDs(virNetMessagePtr msg);
int virNetMessageDecodeNumFDs(virNetMessagePtr msg);

int virNetMessageEncodePayloadRaw(virNetMessagePtr msg,
                                  const char *buf,
                                  size_t len)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;
int virNetMessageEncodePayloadEmpty(virNetMessagePtr msg)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;

void virNetMessageSaveError(virNetMessageErrorPtr rerr)
    ATTRIBUTE_NONNULL(1);

int virNetMessageDupFD(virNetMessagePtr msg,
                       size_t slot);

int virNetMessageAddFD(virNetMessagePtr msg,
                       int fd);
