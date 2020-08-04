/*
 * remote_daemon_dispatch.h: handlers for RPC method calls
 *
 * Copyright (C) 2007-2018 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "remote_protocol.h"
#include "rpc/virnetserverprogram.h"
#include "rpc/virnetserverclient.h"


extern virNetServerProgramProc remoteProcs[];
extern size_t remoteNProcs;

extern virNetServerProgramProc lxcProcs[];
extern size_t lxcNProcs;

extern virNetServerProgramProc qemuProcs[];
extern size_t qemuNProcs;

void remoteClientFree(void *data);
void *remoteClientNew(virNetServerClientPtr client,
                      void *opaque);
