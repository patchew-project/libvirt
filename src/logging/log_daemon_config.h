/*
 * log_daemon_config.h: virtlogd config file handling
 *
 * Copyright (C) 2006-2015 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

typedef struct _virLogDaemonConfig virLogDaemonConfig;
typedef virLogDaemonConfig *virLogDaemonConfigPtr;

struct _virLogDaemonConfig {
    unsigned int log_level;
    char *log_filters;
    char *log_outputs;
    unsigned int max_clients;
    unsigned int admin_max_clients;

    size_t max_backups;
    size_t max_size;
};


int virLogDaemonConfigFilePath(bool privileged, char **configfile);
virLogDaemonConfigPtr virLogDaemonConfigNew(bool privileged);
void virLogDaemonConfigFree(virLogDaemonConfigPtr data);
int virLogDaemonConfigLoadFile(virLogDaemonConfigPtr data,
                               const char *filename,
                               bool allow_missing);
