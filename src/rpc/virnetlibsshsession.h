/*
 * virnetlibsshsession.h: ssh transport provider based on libssh
 *
 * Copyright (C) 2012-2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "viruri.h"

typedef struct _virNetLibsshSession virNetLibsshSession;
typedef virNetLibsshSession *virNetLibsshSessionPtr;

virNetLibsshSessionPtr virNetLibsshSessionNew(const char *username);
void virNetLibsshSessionFree(virNetLibsshSessionPtr sess);

typedef enum {
    VIR_NET_LIBSSH_HOSTKEY_VERIFY_NORMAL,
    VIR_NET_LIBSSH_HOSTKEY_VERIFY_AUTO_ADD,
    VIR_NET_LIBSSH_HOSTKEY_VERIFY_IGNORE
} virNetLibsshHostkeyVerify;

void virNetLibsshSessionSetChannelCommand(virNetLibsshSessionPtr sess,
                                          const char *command);

int virNetLibsshSessionAuthSetCallback(virNetLibsshSessionPtr sess,
                                       virConnectAuthPtr auth);

int virNetLibsshSessionAuthAddPasswordAuth(virNetLibsshSessionPtr sess,
                                           virURIPtr uri);

int virNetLibsshSessionAuthAddAgentAuth(virNetLibsshSessionPtr sess);

int virNetLibsshSessionAuthAddPrivKeyAuth(virNetLibsshSessionPtr sess,
                                          const char *keyfile,
                                          const char *password);

int virNetLibsshSessionAuthAddKeyboardAuth(virNetLibsshSessionPtr sess,
                                           int tries);

int virNetLibsshSessionSetHostKeyVerification(virNetLibsshSessionPtr sess,
                                              const char *hostname,
                                              int port,
                                              const char *hostsfile,
                                              virNetLibsshHostkeyVerify opt);

int virNetLibsshSessionConnect(virNetLibsshSessionPtr sess,
                               int sock);

ssize_t virNetLibsshChannelRead(virNetLibsshSessionPtr sess,
                                char *buf,
                                size_t len);

ssize_t virNetLibsshChannelWrite(virNetLibsshSessionPtr sess,
                                 const char *buf,
                                 size_t len);

bool virNetLibsshSessionHasCachedData(virNetLibsshSessionPtr sess);
