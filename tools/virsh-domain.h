/*
 * virsh-domain.h: Commands to manage domain
 *
 * Copyright (C) 2005, 2007-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virsh.h"

struct virshDomainEventCallback {
    const char *name;
    virConnectDomainEventGenericCallback cb;
};
typedef struct virshDomainEventCallback virshDomainEventCallback;

extern virshDomainEventCallback virshDomainEventCallbacks[];

typedef enum {
    VIRSH_DOMAIN_HOSTNAME_SOURCE_AGENT,
    VIRSH_DOMAIN_HOSTNAME_SOURCE_LEASE,
    VIRSH_DOMAIN_HOSTNAME_SOURCE_LAST
} virshDomainHostnameSource;

VIR_ENUM_DECL(virshDomainHostnameSource);

extern const vshCmdDef domManagementCmds[];
