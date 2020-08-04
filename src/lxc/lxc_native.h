/*
 * lxc_native.h: LXC native configuration import
 *
 * Copyright (c) 2013 SUSE LINUX Products GmbH, Nuernberg, Germany.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "domain_conf.h"
#include "virenum.h"

#define LXC_CONFIG_FORMAT "lxc-tools"

typedef enum {
    VIR_LXC_NETWORK_CONFIG_NAME,
    VIR_LXC_NETWORK_CONFIG_TYPE,
    VIR_LXC_NETWORK_CONFIG_LINK,
    VIR_LXC_NETWORK_CONFIG_HWADDR,
    VIR_LXC_NETWORK_CONFIG_FLAGS,
    VIR_LXC_NETWORK_CONFIG_MACVLAN_MODE,
    VIR_LXC_NETWORK_CONFIG_VLAN_ID,
    VIR_LXC_NETWORK_CONFIG_IPV4,
    VIR_LXC_NETWORK_CONFIG_IPV4_GATEWAY,
    VIR_LXC_NETWORK_CONFIG_IPV4_ADDRESS,
    VIR_LXC_NETWORK_CONFIG_IPV6,
    VIR_LXC_NETWORK_CONFIG_IPV6_GATEWAY,
    VIR_LXC_NETWORK_CONFIG_IPV6_ADDRESS,
    VIR_LXC_NETWORK_CONFIG_LAST,
} virLXCNetworkConfigEntry;

VIR_ENUM_DECL(virLXCNetworkConfigEntry);

virDomainDefPtr lxcParseConfigString(const char *config,
                                     virCapsPtr caps,
                                     virDomainXMLOptionPtr xmlopt);
