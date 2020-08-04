/*
 * lock_daemon.h: lock management daemon
 *
 * Copyright (C) 2006-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virlockspace.h"
#include "virthread.h"

typedef struct _virLockDaemon virLockDaemon;
typedef virLockDaemon *virLockDaemonPtr;

typedef struct _virLockDaemonClient virLockDaemonClient;
typedef virLockDaemonClient *virLockDaemonClientPtr;

struct _virLockDaemonClient {
    virMutex lock;
    bool restricted;

    pid_t ownerPid;
    char *ownerName;
    unsigned char ownerUUID[VIR_UUID_BUFLEN];
    unsigned int ownerId;

    pid_t clientPid;
};

extern virLockDaemonPtr lockDaemon;

int virLockDaemonAddLockSpace(virLockDaemonPtr lockd,
                              const char *path,
                              virLockSpacePtr lockspace);

virLockSpacePtr virLockDaemonFindLockSpace(virLockDaemonPtr lockd,
                                           const char *path);
