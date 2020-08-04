/*
 * virfirewalld.h: support for firewalld (https://firewalld.org)
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#define VIR_FIREWALL_FIREWALLD_SERVICE "org.fedoraproject.FirewallD1"

typedef enum {
    VIR_FIREWALLD_BACKEND_NONE,
    VIR_FIREWALLD_BACKEND_IPTABLES,
    VIR_FIREWALLD_BACKEND_NFTABLES,
    VIR_FIREWALLD_BACKEND_LAST,
} virFirewallDBackendType;

int virFirewallDGetVersion(unsigned long *version);
int virFirewallDGetBackend(void);
int virFirewallDIsRegistered(void);
int virFirewallDGetZones(char ***zones, size_t *nzones);
bool virFirewallDZoneExists(const char *match);
int virFirewallDApplyRule(virFirewallLayer layer,
                          char **args, size_t argsLen,
                          bool ignoreErrors,
                          char **output);

int virFirewallDInterfaceSetZone(const char *iface,
                                 const char *zone);
