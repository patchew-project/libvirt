/*
 * virpolkit.h: helpers for using polkit APIs
 *
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "vircommand.h"

#define PKTTYAGENT "/usr/bin/pkttyagent"

int virPolkitCheckAuth(const char *actionid,
                       pid_t pid,
                       unsigned long long startTime,
                       uid_t uid,
                       const char **details,
                       bool allowInteraction);

typedef struct _virPolkitAgent virPolkitAgent;
typedef virPolkitAgent *virPolkitAgentPtr;

void virPolkitAgentDestroy(virPolkitAgentPtr cmd);
virPolkitAgentPtr virPolkitAgentCreate(void);
