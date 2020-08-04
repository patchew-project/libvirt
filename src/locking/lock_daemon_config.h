/*
 * lock_daemon_config.h: virtlockd config file handling
 *
 * Copyright (C) 2006-2012 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

typedef struct _virLockDaemonConfig virLockDaemonConfig;
typedef virLockDaemonConfig *virLockDaemonConfigPtr;

struct _virLockDaemonConfig {
    unsigned int log_level;
    char *log_filters;
    char *log_outputs;
    unsigned int max_clients;
    unsigned int admin_max_clients;
};


int virLockDaemonConfigFilePath(bool privileged, char **configfile);
virLockDaemonConfigPtr virLockDaemonConfigNew(bool privileged);
void virLockDaemonConfigFree(virLockDaemonConfigPtr data);
int virLockDaemonConfigLoadFile(virLockDaemonConfigPtr data,
                                const char *filename,
                                bool allow_missing);
