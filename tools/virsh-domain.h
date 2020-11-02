/*
 * virsh-domain.h: Commands to manage domain
 *
 * Copyright (C) 2005, 2007-2012 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
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

#define VIRSH_DOMAIN_OPT_INTERFACE(_helpstr, oflags, cflags) \
    {.name = "interface", \
     .type = VSH_OT_STRING, \
     .flags = oflags, \
     .help = _helpstr, \
     .completer = virshDomainInterfaceCompleter, \
     .completer_flags = cflags, \
    }

#define VIRSH_DOMAIN_OPT_MAC(_helpstr, oflags) \
    {.name = "mac", \
     .type = VSH_OT_STRING, \
     .flags = oflags, \
     .help = _helpstr, \
     .completer = virshDomainInterfaceCompleter, \
     .completer_flags = VIRSH_DOMAIN_INTERFACE_COMPLETER_MAC, \
    }
