/*
 * admin_server_dispatch.h: handlers for admin RPC method calls
 *
 * Copyright (C) 2014-2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "rpc/virnetserverprogram.h"
#include "rpc/virnetserverclient.h"
#include "admin/admin_protocol.h"


extern virNetServerProgramProc adminProcs[];
extern size_t adminNProcs;

void remoteAdmClientFree(void *data);
void *remoteAdmClientNew(virNetServerClientPtr client, void *opaque);
void *remoteAdmClientNewPostExecRestart(virNetServerClientPtr client,
                                        virJSONValuePtr object,
                                        void *opaque);
virJSONValuePtr remoteAdmClientPreExecRestart(virNetServerClientPtr client,
                                              void *data);
