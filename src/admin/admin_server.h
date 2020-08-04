/*
 * admin_server.h: admin methods to manage daemons and clients
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "rpc/virnetdaemon.h"
#include "rpc/virnetserver.h"

int adminConnectListServers(virNetDaemonPtr dmn,
                            virNetServerPtr **servers,
                            unsigned int flags);

virNetServerPtr adminConnectLookupServer(virNetDaemonPtr dmn,
                                         const char *name,
                                         unsigned int flags);

int
adminServerGetThreadPoolParameters(virNetServerPtr srv,
                                   virTypedParameterPtr *params,
                                   int *nparams,
                                   unsigned int flags);
int
adminServerSetThreadPoolParameters(virNetServerPtr srv,
                                   virTypedParameterPtr params,
                                   int nparams,
                                   unsigned int flags);

int adminServerListClients(virNetServerPtr srv,
                           virNetServerClientPtr **clients,
                           unsigned int flags);

virNetServerClientPtr adminServerLookupClient(virNetServerPtr srv,
                                              unsigned long long id,
                                              unsigned int flags);

int adminClientGetInfo(virNetServerClientPtr client,
                       virTypedParameterPtr *params,
                       int *nparams,
                       unsigned int flags);

int adminClientClose(virNetServerClientPtr client,
                     unsigned int flags);

int adminServerGetClientLimits(virNetServerPtr srv,
                               virTypedParameterPtr *params,
                               int *nparams,
                               unsigned int flags);

int adminServerSetClientLimits(virNetServerPtr srv,
                               virTypedParameterPtr params,
                               int nparams,
                               unsigned int flags);

int adminServerUpdateTlsFiles(virNetServerPtr srv,
                              unsigned int flags);
