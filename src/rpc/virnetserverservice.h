/*
 * virnetserverservice.h: generic network RPC server service
 *
 * Copyright (C) 2006-2011, 2014 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virnetserverprogram.h"
#include "virobject.h"

typedef enum {
    VIR_NET_SERVER_SERVICE_AUTH_NONE = 0,
    VIR_NET_SERVER_SERVICE_AUTH_SASL,
    VIR_NET_SERVER_SERVICE_AUTH_POLKIT,
} virNetServerServiceAuthMethods;

typedef int (*virNetServerServiceDispatchFunc)(virNetServerServicePtr svc,
                                               virNetSocketPtr sock,
                                               void *opaque);

virNetServerServicePtr virNetServerServiceNewTCP(const char *nodename,
                                                 const char *service,
                                                 int family,
                                                 int auth,
                                                 virNetTLSContextPtr tls,
                                                 bool readonly,
                                                 size_t max_queued_clients,
                                                 size_t nrequests_client_max);
virNetServerServicePtr virNetServerServiceNewUNIX(const char *path,
                                                  mode_t mask,
                                                  gid_t grp,
                                                  int auth,
                                                  virNetTLSContextPtr tls,
                                                  bool readonly,
                                                  size_t max_queued_clients,
                                                  size_t nrequests_client_max);
virNetServerServicePtr virNetServerServiceNewFDs(int *fd,
                                                 size_t nfds,
                                                 bool unlinkUNIX,
                                                 int auth,
                                                 virNetTLSContextPtr tls,
                                                 bool readonly,
                                                 size_t max_queued_clients,
                                                 size_t nrequests_client_max);

virNetServerServicePtr virNetServerServiceNewPostExecRestart(virJSONValuePtr object);

virJSONValuePtr virNetServerServicePreExecRestart(virNetServerServicePtr service);

int virNetServerServiceGetPort(virNetServerServicePtr svc);

int virNetServerServiceGetAuth(virNetServerServicePtr svc);
bool virNetServerServiceIsReadonly(virNetServerServicePtr svc);
size_t virNetServerServiceGetMaxRequests(virNetServerServicePtr svc);
virNetTLSContextPtr virNetServerServiceGetTLSContext(virNetServerServicePtr svc);

void virNetServerServiceSetDispatcher(virNetServerServicePtr svc,
                                      virNetServerServiceDispatchFunc func,
                                      void *opaque);

void virNetServerServiceToggle(virNetServerServicePtr svc,
                               bool enabled);

void virNetServerServiceClose(virNetServerServicePtr svc);
