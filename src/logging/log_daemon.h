/*
 * log_daemon.h: log management daemon
 *
 * Copyright (C) 2006-2015 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virthread.h"
#include "log_handler.h"

typedef struct _virLogDaemon virLogDaemon;
typedef virLogDaemon *virLogDaemonPtr;

typedef struct _virLogDaemonClient virLogDaemonClient;
typedef virLogDaemonClient *virLogDaemonClientPtr;

struct _virLogDaemonClient {
    virMutex lock;

    pid_t clientPid;
};

extern virLogDaemonPtr logDaemon;

virLogHandlerPtr virLogDaemonGetHandler(virLogDaemonPtr dmn);
