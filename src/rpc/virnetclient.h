/*
 * virnetclient.h: generic network RPC client
 *
 * Copyright (C) 2006-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virnettlscontext.h"
#include "virnetmessage.h"
#ifdef WITH_SASL
# include "virnetsaslcontext.h"
#endif
#include "virnetclientprogram.h"
#include "virnetclientstream.h"
#include "virobject.h"
#include "viruri.h"


virNetClientPtr virNetClientNewUNIX(const char *path,
                                    bool spawnDaemon,
                                    const char *binary);

virNetClientPtr virNetClientNewTCP(const char *nodename,
                                   const char *service,
                                   int family);

virNetClientPtr virNetClientNewSSH(const char *nodename,
                                   const char *service,
                                   const char *binary,
                                   const char *username,
                                   bool noTTY,
                                   bool noVerify,
                                   const char *netcat,
                                   const char *keyfile,
                                   const char *path);

virNetClientPtr virNetClientNewLibSSH2(const char *host,
                                       const char *port,
                                       int family,
                                       const char *username,
                                       const char *privkeyPath,
                                       const char *knownHostsPath,
                                       const char *knownHostsVerify,
                                       const char *authMethods,
                                       const char *netcatPath,
                                       const char *socketPath,
                                       virConnectAuthPtr authPtr,
                                       virURIPtr uri);

virNetClientPtr virNetClientNewLibssh(const char *host,
                                      const char *port,
                                      int family,
                                      const char *username,
                                      const char *privkeyPath,
                                      const char *knownHostsPath,
                                      const char *knownHostsVerify,
                                      const char *authMethods,
                                      const char *netcatPath,
                                      const char *socketPath,
                                      virConnectAuthPtr authPtr,
                                      virURIPtr uri);

virNetClientPtr virNetClientNewExternal(const char **cmdargv);

int virNetClientRegisterAsyncIO(virNetClientPtr client);
int virNetClientRegisterKeepAlive(virNetClientPtr client);

typedef void (*virNetClientCloseFunc)(virNetClientPtr client,
                                      int reason,
                                      void *opaque);

void virNetClientSetCloseCallback(virNetClientPtr client,
                                  virNetClientCloseFunc cb,
                                  void *opaque,
                                  virFreeCallback ff);

int virNetClientGetFD(virNetClientPtr client);
int virNetClientDupFD(virNetClientPtr client, bool cloexec);

bool virNetClientHasPassFD(virNetClientPtr client);

int virNetClientAddProgram(virNetClientPtr client,
                           virNetClientProgramPtr prog);

int virNetClientAddStream(virNetClientPtr client,
                          virNetClientStreamPtr st);

void virNetClientRemoveStream(virNetClientPtr client,
                              virNetClientStreamPtr st);

int virNetClientSendWithReply(virNetClientPtr client,
                              virNetMessagePtr msg);

int virNetClientSendNonBlock(virNetClientPtr client,
                             virNetMessagePtr msg);

int virNetClientSendStream(virNetClientPtr client,
                           virNetMessagePtr msg,
                           virNetClientStreamPtr st);

#ifdef WITH_SASL
void virNetClientSetSASLSession(virNetClientPtr client,
                                virNetSASLSessionPtr sasl);
#endif

int virNetClientSetTLSSession(virNetClientPtr client,
                              virNetTLSContextPtr tls);

bool virNetClientIsEncrypted(virNetClientPtr client);
bool virNetClientIsOpen(virNetClientPtr client);

const char *virNetClientLocalAddrStringSASL(virNetClientPtr client);
const char *virNetClientRemoteAddrStringSASL(virNetClientPtr client);

int virNetClientGetTLSKeySize(virNetClientPtr client);

void virNetClientClose(virNetClientPtr client);

bool virNetClientKeepAliveIsSupported(virNetClientPtr client);
int virNetClientKeepAliveStart(virNetClientPtr client,
                               int interval,
                               unsigned int count);

void virNetClientKeepAliveStop(virNetClientPtr client);
