/*
 * virfirewallpriv.h: integration with firewalls private APIs
 *
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef LIBVIRT_VIRFIREWALLPRIV_H_ALLOW
# error "virfirewallpriv.h may only be included by virfirewall.c or test suites"
#endif /* LIBVIRT_VIRFIREWALLPRIV_H_ALLOW */

#pragma once

#include "virfirewall.h"

typedef enum {
    VIR_FIREWALL_BACKEND_AUTOMATIC,
    VIR_FIREWALL_BACKEND_DIRECT,
    VIR_FIREWALL_BACKEND_FIREWALLD,

    VIR_FIREWALL_BACKEND_LAST,
} virFirewallBackend;

int virFirewallSetBackend(virFirewallBackend backend);
