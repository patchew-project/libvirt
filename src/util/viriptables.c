/*
 * viriptables.c: helper APIs for managing iptables
 *
 * Copyright (C) 2007-2014 Red Hat, Inc.
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
 *
 * Authors:
 *     Mark McLoughlin <markmc@redhat.com>
 */

#include <config.h>

#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "internal.h"
#include "viriptables.h"
#include "vircommand.h"
#include "viralloc.h"
#include "virerror.h"
#include "virfile.h"
#include "virlog.h"
#include "virthread.h"
#include "virstring.h"
#include "virutil.h"

VIR_LOG_INIT("util.iptables");

#define VIR_FROM_THIS VIR_FROM_NONE

enum {
    ADD = 0,
    REMOVE
};

enum {
    VIR_IPTABLES_CHAIN_BUILTIN,
    VIR_IPTABLES_CHAIN_PRIVATE,

    VIR_IPTABLES_CHAIN_LAST,
};


typedef struct {
    virFirewallLayer layer;
    const char *table;
    const char *parent;
    const char *child;
} iptablesChain;

static int
iptablesCheckPrivateChain(virFirewallPtr fw,
                          const char *const *lines,
                          void *opaque)
{
    iptablesChain *data = opaque;
    bool found = false;

    while (lines && *lines && !found) {
        if (STRPREFIX(*lines, data->child))
            found = true;
        lines++;
    }

    if (!found)
        virFirewallAddRule(fw, data->layer,
                           "--table", data->table,
                           "--insert", data->parent,
                           "--jump", data->child, NULL);

     return 0;
}


int
iptablesSetupPrivateChains(void)
{
    virFirewallPtr fw;
    int ret = -1;
    iptablesChain chains[] = {
        {VIR_FIREWALL_LAYER_IPV4, "filter", "INPUT", "INP_libvirt"},
        {VIR_FIREWALL_LAYER_IPV4, "filter", "OUTPUT", "OUT_libvirt"},
        {VIR_FIREWALL_LAYER_IPV4, "filter", "FORWARD", "FWD_libvirt_out"},
        {VIR_FIREWALL_LAYER_IPV4, "filter", "FORWARD", "FWD_libvirt_in"},
        {VIR_FIREWALL_LAYER_IPV4, "filter", "FORWARD", "FWD_libvirt_cross"},
        {VIR_FIREWALL_LAYER_IPV4, "nat", "POSTROUTING", "PRT_libvirt"},
        {VIR_FIREWALL_LAYER_IPV6, "filter", "INPUT", "INP_libvirt"},
        {VIR_FIREWALL_LAYER_IPV6, "filter", "OUTPUT", "OUT_libvirt"},
        {VIR_FIREWALL_LAYER_IPV6, "filter", "FORWARD", "FWD_libvirt_out"},
        {VIR_FIREWALL_LAYER_IPV6, "filter", "FORWARD", "FWD_libvirt_in"},
        {VIR_FIREWALL_LAYER_IPV6, "filter", "FORWARD", "FWD_libvirt_cross"},
        {VIR_FIREWALL_LAYER_IPV6, "nat", "POSTROUTING", "PRT_libvirt"},
    };
    size_t i;

    fw = virFirewallNew();

    virFirewallStartTransaction(fw, VIR_FIREWALL_TRANSACTION_IGNORE_ERRORS);

    for (i = 0; i < ARRAY_CARDINALITY(chains); i++) {
        virFirewallAddRule(fw, chains[i].layer,
                           "--table", chains[i].table,
                           "--new-chain", chains[i].child, NULL);
    }

    virFirewallStartTransaction(fw, 0);

    for (i = 0; i < ARRAY_CARDINALITY(chains); i++) {
        virFirewallAddRuleFull(fw, chains[i].layer,
                               false, iptablesCheckPrivateChain,
                               &chains[i],
                               "--table", chains[i].table,
                               "--list", chains[i].parent, NULL);
    }

    if (virFirewallApply(fw) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    return ret;
}

static void
iptablesInput(virFirewallPtr fw,
              virFirewallLayer layer,
              int chain,
              const char *iface,
              int port,
              int action,
              int tcp)
{
    char portstr[32];
    static const char *chainName[VIR_IPTABLES_CHAIN_LAST] = {
        "INPUT",
        "INP_libvirt",
    };

    snprintf(portstr, sizeof(portstr), "%d", port);
    portstr[sizeof(portstr) - 1] = '\0';

    virFirewallAddRule(fw, layer,
                       "--table", "filter",
                       action == ADD ? "--insert" : "--delete", chainName[chain],
                       "--in-interface", iface,
                       "--protocol", tcp ? "tcp" : "udp",
                       "--destination-port", portstr,
                       "--jump", "ACCEPT",
                       NULL);
}

static void
iptablesOutput(virFirewallPtr fw,
               virFirewallLayer layer,
               int chain,
               const char *iface,
               int port,
               int action,
               int tcp)
{
    char portstr[32];
    static const char *chainName[VIR_IPTABLES_CHAIN_LAST] = {
        "OUTPUT",
        "OUT_libvirt",
    };

    snprintf(portstr, sizeof(portstr), "%d", port);
    portstr[sizeof(portstr) - 1] = '\0';

    virFirewallAddRule(fw, layer,
                       "--table", "filter",
                       action == ADD ? "--insert" : "--delete", chainName[chain],
                       "--out-interface", iface,
                       "--protocol", tcp ? "tcp" : "udp",
                       "--destination-port", portstr,
                       "--jump", "ACCEPT",
                       NULL);
}

/**
 * iptablesAddTcpInput:
 * @ctx: pointer to the IP table context
 * @iface: the interface name
 * @port: the TCP port to add
 *
 * Add an input to the IP table allowing access to the given @port on
 * the given @iface interface for TCP packets
 */
void
iptablesAddTcpInput(virFirewallPtr fw,
                    virFirewallLayer layer,
                    const char *iface,
                    int port)
{
    iptablesInput(fw, layer, VIR_IPTABLES_CHAIN_BUILTIN, iface, port, ADD, 1);
}

/**
 * iptablesRemoveTcpInput:
 * @ctx: pointer to the IP table context
 * @iface: the interface name
 * @port: the TCP port to remove
 *
 * Removes an input from the IP table, hence forbidding access to the given
 * @port on the given @iface interface for TCP packets
 */
void
iptablesRemoveTcpInput(virFirewallPtr fw,
                       virFirewallLayer layer,
                       const char *iface,
                       int port)
{
    iptablesInput(fw, layer, VIR_IPTABLES_CHAIN_BUILTIN, iface, port, REMOVE, 1);
}

/**
 * iptablesAddUdpInput:
 * @ctx: pointer to the IP table context
 * @iface: the interface name
 * @port: the UDP port to add
 *
 * Add an input to the IP table allowing access to the given @port on
 * the given @iface interface for UDP packets
 */
void
iptablesAddUdpInput(virFirewallPtr fw,
                    virFirewallLayer layer,
                    const char *iface,
                    int port)
{
    iptablesInput(fw, layer, VIR_IPTABLES_CHAIN_BUILTIN, iface, port, ADD, 0);
}

/**
 * iptablesRemoveUdpInput:
 * @ctx: pointer to the IP table context
 * @iface: the interface name
 * @port: the UDP port to remove
 *
 * Removes an input from the IP table, hence forbidding access to the given
 * @port on the given @iface interface for UDP packets
 */
void
iptablesRemoveUdpInput(virFirewallPtr fw,
                       virFirewallLayer layer,
                       const char *iface,
                       int port)
{
    return iptablesInput(fw, layer, VIR_IPTABLES_CHAIN_BUILTIN, iface, port, REMOVE, 0);
}

/**
 * iptablesAddUdpOutput:
 * @ctx: pointer to the IP table context
 * @iface: the interface name
 * @port: the UDP port to add
 *
 * Add an output to the IP table allowing access to the given @port from
 * the given @iface interface for UDP packets
 */
void
iptablesAddUdpOutput(virFirewallPtr fw,
                     virFirewallLayer layer,
                     const char *iface,
                     int port)
{
    iptablesOutput(fw, layer, VIR_IPTABLES_CHAIN_BUILTIN, iface, port, ADD, 0);
}

/**
 * iptablesRemoveUdpOutput:
 * @ctx: pointer to the IP table context
 * @iface: the interface name
 * @port: the UDP port to remove
 *
 * Removes an output from the IP table, hence forbidding access to the given
 * @port from the given @iface interface for UDP packets
 */
void
iptablesRemoveUdpOutput(virFirewallPtr fw,
                        virFirewallLayer layer,
                        const char *iface,
                        int port)
{
    iptablesOutput(fw, layer, VIR_IPTABLES_CHAIN_BUILTIN, iface, port, REMOVE, 0);
}


static char *iptablesFormatNetwork(virSocketAddr *netaddr,
                                   unsigned int prefix)
{
    virSocketAddr network;
    VIR_AUTOFREE(char *) netstr = NULL;
    char *ret;

    if (!(VIR_SOCKET_ADDR_IS_FAMILY(netaddr, AF_INET) ||
          VIR_SOCKET_ADDR_IS_FAMILY(netaddr, AF_INET6))) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("Only IPv4 or IPv6 addresses can be used with iptables"));
        return NULL;
    }

    if (virSocketAddrMaskByPrefix(netaddr, prefix, &network) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Failure to mask address"));
        return NULL;
    }

    netstr = virSocketAddrFormat(&network);

    if (!netstr)
        return NULL;

    ignore_value(virAsprintf(&ret, "%s/%d", netstr, prefix));

    return ret;
}


/* Allow all traffic coming from the bridge, with a valid network address
 * to proceed to WAN
 */
static int
iptablesForwardAllowOut(virFirewallPtr fw,
                        int chain,
                        virSocketAddr *netaddr,
                        unsigned int prefix,
                        const char *iface,
                        const char *physdev,
                        int action)
{
    VIR_AUTOFREE(char *) networkstr = NULL;
    virFirewallLayer layer = VIR_SOCKET_ADDR_FAMILY(netaddr) == AF_INET ?
        VIR_FIREWALL_LAYER_IPV4 : VIR_FIREWALL_LAYER_IPV6;
    static const char *chainName[VIR_IPTABLES_CHAIN_LAST] = {
        "FORWARD",
        "FWD_libvirt_out",
    };

    if (!(networkstr = iptablesFormatNetwork(netaddr, prefix)))
        return -1;

    if (physdev && physdev[0])
        virFirewallAddRule(fw, layer,
                           "--table", "filter",
                           action == ADD ? "--insert" : "--delete", chainName[chain],
                           "--source", networkstr,
                           "--in-interface", iface,
                           "--out-interface", physdev,
                           "--jump", "ACCEPT",
                           NULL);
    else
        virFirewallAddRule(fw, layer,
                           "--table", "filter",
                           action == ADD ? "--insert" : "--delete", chainName[chain],
                           "--source", networkstr,
                           "--in-interface", iface,
                           "--jump", "ACCEPT",
                           NULL);

    return 0;
}

/**
 * iptablesAddForwardAllowOut:
 * @ctx: pointer to the IP table context
 * @network: the source network name
 * @iface: the source interface name
 * @physdev: the physical output device
 *
 * Add a rule to the IP table context to allow the traffic for the
 * network @network via interface @iface to be forwarded to
 * @physdev device. This allow the outbound traffic on a bridge.
 *
 * Returns 0 in case of success or an error code otherwise
 */
int
iptablesAddForwardAllowOut(virFirewallPtr fw,
                           virSocketAddr *netaddr,
                           unsigned int prefix,
                           const char *iface,
                           const char *physdev)
{
    return iptablesForwardAllowOut(fw, VIR_IPTABLES_CHAIN_BUILTIN, netaddr, prefix, iface, physdev, ADD);
}

/**
 * iptablesRemoveForwardAllowOut:
 * @ctx: pointer to the IP table context
 * @network: the source network name
 * @iface: the source interface name
 * @physdev: the physical output device
 *
 * Remove a rule from the IP table context hence forbidding forwarding
 * of the traffic for the network @network via interface @iface
 * to the @physdev device output. This stops the outbound traffic on a bridge.
 *
 * Returns 0 in case of success or an error code otherwise
 */
int
iptablesRemoveForwardAllowOut(virFirewallPtr fw,
                              virSocketAddr *netaddr,
                              unsigned int prefix,
                              const char *iface,
                              const char *physdev)
{
    return iptablesForwardAllowOut(fw, VIR_IPTABLES_CHAIN_BUILTIN, netaddr, prefix, iface, physdev, REMOVE);
}


/* Allow all traffic destined to the bridge, with a valid network address
 * and associated with an existing connection
 */
static int
iptablesForwardAllowRelatedIn(virFirewallPtr fw,
                              int chain,
                              virSocketAddr *netaddr,
                              unsigned int prefix,
                              const char *iface,
                              const char *physdev,
                              int action)
{
    virFirewallLayer layer = VIR_SOCKET_ADDR_FAMILY(netaddr) == AF_INET ?
        VIR_FIREWALL_LAYER_IPV4 : VIR_FIREWALL_LAYER_IPV6;
    VIR_AUTOFREE(char *) networkstr = NULL;
    static const char *chainName[VIR_IPTABLES_CHAIN_LAST] = {
        "FORWARD",
        "FWD_libvirt_in",
    };

    if (!(networkstr = iptablesFormatNetwork(netaddr, prefix)))
        return -1;

    if (physdev && physdev[0])
        virFirewallAddRule(fw, layer,
                           "--table", "filter",
                           action == ADD ? "--insert" : "--delete", chainName[chain],
                           "--destination", networkstr,
                           "--in-interface", physdev,
                           "--out-interface", iface,
                           "--match", "conntrack",
                           "--ctstate", "ESTABLISHED,RELATED",
                           "--jump", "ACCEPT",
                           NULL);
    else
        virFirewallAddRule(fw, layer,
                           "--table", "filter",
                           action == ADD ? "--insert" : "--delete", chainName[chain],
                           "--destination", networkstr,
                           "--out-interface", iface,
                           "--match", "conntrack",
                           "--ctstate", "ESTABLISHED,RELATED",
                           "--jump", "ACCEPT",
                           NULL);

    return 0;
}

/**
 * iptablesAddForwardAllowRelatedIn:
 * @ctx: pointer to the IP table context
 * @network: the source network name
 * @iface: the output interface name
 * @physdev: the physical input device or NULL
 *
 * Add rules to the IP table context to allow the traffic for the
 * network @network on @physdev device to be forwarded to
 * interface @iface, if it is part of an existing connection.
 *
 * Returns 0 in case of success or an error code otherwise
 */
int
iptablesAddForwardAllowRelatedIn(virFirewallPtr fw,
                                 virSocketAddr *netaddr,
                                 unsigned int prefix,
                                 const char *iface,
                                 const char *physdev)
{
    return iptablesForwardAllowRelatedIn(fw, VIR_IPTABLES_CHAIN_BUILTIN, netaddr, prefix, iface, physdev, ADD);
}

/**
 * iptablesRemoveForwardAllowRelatedIn:
 * @ctx: pointer to the IP table context
 * @network: the source network name
 * @iface: the output interface name
 * @physdev: the physical input device or NULL
 *
 * Remove rules from the IP table context hence forbidding the traffic for
 * network @network on @physdev device to be forwarded to
 * interface @iface, if it is part of an existing connection.
 *
 * Returns 0 in case of success or an error code otherwise
 */
int
iptablesRemoveForwardAllowRelatedIn(virFirewallPtr fw,
                                    virSocketAddr *netaddr,
                                    unsigned int prefix,
                                    const char *iface,
                                    const char *physdev)
{
    return iptablesForwardAllowRelatedIn(fw, VIR_IPTABLES_CHAIN_BUILTIN, netaddr, prefix, iface, physdev, REMOVE);
}

/* Allow all traffic destined to the bridge, with a valid network address
 */
static int
iptablesForwardAllowIn(virFirewallPtr fw,
                       int chain,
                       virSocketAddr *netaddr,
                       unsigned int prefix,
                       const char *iface,
                       const char *physdev,
                       int action)
{
    virFirewallLayer layer = VIR_SOCKET_ADDR_FAMILY(netaddr) == AF_INET ?
        VIR_FIREWALL_LAYER_IPV4 : VIR_FIREWALL_LAYER_IPV6;
    VIR_AUTOFREE(char *) networkstr = NULL;
    static const char *chainName[VIR_IPTABLES_CHAIN_LAST] = {
        "FORWARD",
        "FWD_libvirt_in",
    };

    if (!(networkstr = iptablesFormatNetwork(netaddr, prefix)))
        return -1;

    if (physdev && physdev[0])
        virFirewallAddRule(fw, layer,
                           "--table", "filter",
                           action == ADD ? "--insert" : "--delete", chainName[chain],
                           "--destination", networkstr,
                           "--in-interface", physdev,
                           "--out-interface", iface,
                           "--jump", "ACCEPT",
                           NULL);
    else
        virFirewallAddRule(fw, layer,
                           "--table", "filter",
                           action == ADD ? "--insert" : "--delete", chainName[chain],
                           "--destination", networkstr,
                           "--out-interface", iface,
                           "--jump", "ACCEPT",
                           NULL);
    return 0;
}

/**
 * iptablesAddForwardAllowIn:
 * @ctx: pointer to the IP table context
 * @network: the source network name
 * @iface: the output interface name
 * @physdev: the physical input device or NULL
 *
 * Add rules to the IP table context to allow the traffic for the
 * network @network on @physdev device to be forwarded to
 * interface @iface. This allow the inbound traffic on a bridge.
 *
 * Returns 0 in case of success or an error code otherwise
 */
int
iptablesAddForwardAllowIn(virFirewallPtr fw,
                          virSocketAddr *netaddr,
                          unsigned int prefix,
                          const char *iface,
                          const char *physdev)
{
    return iptablesForwardAllowIn(fw, VIR_IPTABLES_CHAIN_BUILTIN, netaddr, prefix, iface, physdev, ADD);
}

/**
 * iptablesRemoveForwardAllowIn:
 * @ctx: pointer to the IP table context
 * @network: the source network name
 * @iface: the output interface name
 * @physdev: the physical input device or NULL
 *
 * Remove rules from the IP table context hence forbidding the traffic for
 * network @network on @physdev device to be forwarded to
 * interface @iface. This stops the inbound traffic on a bridge.
 *
 * Returns 0 in case of success or an error code otherwise
 */
int
iptablesRemoveForwardAllowIn(virFirewallPtr fw,
                             virSocketAddr *netaddr,
                             unsigned int prefix,
                             const char *iface,
                             const char *physdev)
{
    return iptablesForwardAllowIn(fw, VIR_IPTABLES_CHAIN_BUILTIN, netaddr, prefix, iface, physdev, REMOVE);
}

static void
iptablesForwardAllowCross(virFirewallPtr fw,
                          virFirewallLayer layer,
                          int chain,
                          const char *iface,
                          int action)
{
    static const char *chainName[VIR_IPTABLES_CHAIN_LAST] = {
        "FORWARD",
        "FWD_libvirt_cross",
    };

    virFirewallAddRule(fw, layer,
                       "--table", "filter",
                       action == ADD ? "--insert" : "--delete", chainName[chain],
                       "--in-interface", iface,
                       "--out-interface", iface,
                       "--jump", "ACCEPT",
                       NULL);
}

/**
 * iptablesAddForwardAllowCross:
 * @ctx: pointer to the IP table context
 * @iface: the input/output interface name
 *
 * Add rules to the IP table context to allow traffic to cross that
 * interface. It allows all traffic between guests on the same bridge
 * represented by that interface.
 *
 * Returns 0 in case of success or an error code otherwise
 */
void
iptablesAddForwardAllowCross(virFirewallPtr fw,
                             virFirewallLayer layer,
                             const char *iface)
{
    iptablesForwardAllowCross(fw, layer, VIR_IPTABLES_CHAIN_BUILTIN, iface, ADD);
}

/**
 * iptablesRemoveForwardAllowCross:
 * @ctx: pointer to the IP table context
 * @iface: the input/output interface name
 *
 * Remove rules to the IP table context to block traffic to cross that
 * interface. It forbids traffic between guests on the same bridge
 * represented by that interface.
 *
 * Returns 0 in case of success or an error code otherwise
 */
void
iptablesRemoveForwardAllowCross(virFirewallPtr fw,
                                virFirewallLayer layer,
                                const char *iface)
{
    iptablesForwardAllowCross(fw, layer, VIR_IPTABLES_CHAIN_BUILTIN, iface, REMOVE);
}

static void
iptablesForwardRejectOut(virFirewallPtr fw,
                         virFirewallLayer layer,
                         int chain,
                         const char *iface,
                         int action)
{
    static const char *chainName[VIR_IPTABLES_CHAIN_LAST] = {
        "FORWARD",
        "FWD_libvirt_out",
    };

    virFirewallAddRule(fw, layer,
                       "--table", "filter",
                       action == ADD ? "--insert" : "delete", chainName[chain],
                       "--in-interface", iface,
                       "--jump", "REJECT",
                       NULL);
}

/**
 * iptablesAddForwardRejectOut:
 * @ctx: pointer to the IP table context
 * @iface: the output interface name
 *
 * Add rules to the IP table context to forbid all traffic to that
 * interface. It forbids forwarding from the bridge to that interface.
 *
 * Returns 0 in case of success or an error code otherwise
 */
void
iptablesAddForwardRejectOut(virFirewallPtr fw,
                            virFirewallLayer layer,
                            const char *iface)
{
    iptablesForwardRejectOut(fw, layer, VIR_IPTABLES_CHAIN_BUILTIN, iface, ADD);
}

/**
 * iptablesRemoveForwardRejectOut:
 * @ctx: pointer to the IP table context
 * @iface: the output interface name
 *
 * Remove rules from the IP table context forbidding all traffic to that
 * interface. It reallow forwarding from the bridge to that interface.
 *
 * Returns 0 in case of success or an error code otherwise
 */
void
iptablesRemoveForwardRejectOut(virFirewallPtr fw,
                               virFirewallLayer layer,
                               const char *iface)
{
    iptablesForwardRejectOut(fw, layer, VIR_IPTABLES_CHAIN_BUILTIN, iface, REMOVE);
}


static void
iptablesForwardRejectIn(virFirewallPtr fw,
                        virFirewallLayer layer,
                        int chain,
                        const char *iface,
                        int action)
{
    static const char *chainName[VIR_IPTABLES_CHAIN_LAST] = {
        "FORWARD",
        "FWD_libvirt_in",
    };

    virFirewallAddRule(fw, layer,
                       "--table", "filter",
                       action == ADD ? "--insert" : "--delete", chainName[chain],
                       "--out-interface", iface,
                       "--jump", "REJECT",
                       NULL);
}

/**
 * iptablesAddForwardRejectIn:
 * @ctx: pointer to the IP table context
 * @iface: the input interface name
 *
 * Add rules to the IP table context to forbid all traffic from that
 * interface. It forbids forwarding from that interface to the bridge.
 *
 * Returns 0 in case of success or an error code otherwise
 */
void
iptablesAddForwardRejectIn(virFirewallPtr fw,
                           virFirewallLayer layer,
                           const char *iface)
{
    iptablesForwardRejectIn(fw, layer, VIR_IPTABLES_CHAIN_BUILTIN, iface, ADD);
}

/**
 * iptablesRemoveForwardRejectIn:
 * @ctx: pointer to the IP table context
 * @iface: the input interface name
 *
 * Remove rules from the IP table context forbidding all traffic from that
 * interface. It allows forwarding from that interface to the bridge.
 *
 * Returns 0 in case of success or an error code otherwise
 */
void
iptablesRemoveForwardRejectIn(virFirewallPtr fw,
                              virFirewallLayer layer,
                              const char *iface)
{
    iptablesForwardRejectIn(fw, layer, VIR_IPTABLES_CHAIN_BUILTIN, iface, REMOVE);
}


/* Masquerade all traffic coming from the network associated
 * with the bridge
 */
static int
iptablesForwardMasquerade(virFirewallPtr fw,
                          int chain,
                          virSocketAddr *netaddr,
                          unsigned int prefix,
                          const char *physdev,
                          virSocketAddrRangePtr addr,
                          virPortRangePtr port,
                          const char *protocol,
                          int action)
{
    VIR_AUTOFREE(char *) networkstr = NULL;
    VIR_AUTOFREE(char *) addrStartStr = NULL;
    VIR_AUTOFREE(char *) addrEndStr = NULL;
    VIR_AUTOFREE(char *) portRangeStr = NULL;
    VIR_AUTOFREE(char *) natRangeStr = NULL;
    virFirewallRulePtr rule;
    static const char *chainName[VIR_IPTABLES_CHAIN_LAST] = {
        "POSTROUTING",
        "PRT_libvirt",
    };

    if (!(networkstr = iptablesFormatNetwork(netaddr, prefix)))
        return -1;

    if (!VIR_SOCKET_ADDR_IS_FAMILY(netaddr, AF_INET)) {
        /* Higher level code *should* guaranteee it's impossible to get here. */
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Attempted to NAT '%s'. NAT is only supported for IPv4."),
                       networkstr);
        return -1;
    }

    if (VIR_SOCKET_ADDR_IS_FAMILY(&addr->start, AF_INET)) {
        if (!(addrStartStr = virSocketAddrFormat(&addr->start)))
            return -1;
        if (VIR_SOCKET_ADDR_IS_FAMILY(&addr->end, AF_INET)) {
            if (!(addrEndStr = virSocketAddrFormat(&addr->end)))
                return -1;
        }
    }

    if (protocol && protocol[0]) {
        rule = virFirewallAddRule(fw, VIR_FIREWALL_LAYER_IPV4,
                                  "--table", "nat",
                                  action == ADD ? "--insert" : "--delete", chainName[chain],
                                  "--source", networkstr,
                                  "-p", protocol,
                                  "!", "--destination", networkstr,
                                  NULL);
    } else {
        rule = virFirewallAddRule(fw, VIR_FIREWALL_LAYER_IPV4,
                                  "--table", "nat",
                                  action == ADD ? "--insert" : "--delete", chainName[chain],
                                  "--source", networkstr,
                                  "!", "--destination", networkstr,
                                  NULL);
    }

    if (physdev && physdev[0])
        virFirewallRuleAddArgList(fw, rule, "--out-interface", physdev, NULL);

    if (protocol && protocol[0]) {
        if (port->start == 0 && port->end == 0) {
            port->start = 1024;
            port->end = 65535;
        }

        if (port->start < port->end && port->end < 65536) {
            if (virAsprintf(&portRangeStr, ":%u-%u",
                            port->start, port->end) < 0)
                return -1;
        } else {
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("Invalid port range '%u-%u'."),
                           port->start, port->end);
        }
    }

    /* Use --jump SNAT if public addr is specified */
    if (addrStartStr && addrStartStr[0]) {
        int r = 0;

        if (addrEndStr && addrEndStr[0]) {
            r = virAsprintf(&natRangeStr, "%s-%s%s", addrStartStr, addrEndStr,
                            portRangeStr ? portRangeStr : "");
        } else {
            r = virAsprintf(&natRangeStr, "%s%s", addrStartStr,
                            portRangeStr ? portRangeStr : "");
        }

        if (r < 0)
            return -1;

        virFirewallRuleAddArgList(fw, rule,
                                  "--jump", "SNAT",
                                  "--to-source", natRangeStr, NULL);
    } else {
        virFirewallRuleAddArgList(fw, rule,
                                  "--jump", "MASQUERADE", NULL);

        if (portRangeStr && portRangeStr[0])
            virFirewallRuleAddArgList(fw, rule,
                                      "--to-ports", &portRangeStr[1], NULL);
    }

    return 0;
}

/**
 * iptablesAddForwardMasquerade:
 * @ctx: pointer to the IP table context
 * @network: the source network name
 * @physdev: the physical input device or NULL
 * @protocol: the network protocol or NULL
 *
 * Add rules to the IP table context to allow masquerading
 * network @network on @physdev. This allow the bridge to
 * masquerade for that network (on @physdev).
 *
 * Returns 0 in case of success or an error code otherwise
 */
int
iptablesAddForwardMasquerade(virFirewallPtr fw,
                             virSocketAddr *netaddr,
                             unsigned int prefix,
                             const char *physdev,
                             virSocketAddrRangePtr addr,
                             virPortRangePtr port,
                             const char *protocol)
{
    return iptablesForwardMasquerade(fw, VIR_IPTABLES_CHAIN_BUILTIN, netaddr, prefix,
                                     physdev, addr, port, protocol, ADD);
}

/**
 * iptablesRemoveForwardMasquerade:
 * @ctx: pointer to the IP table context
 * @network: the source network name
 * @physdev: the physical input device or NULL
 * @protocol: the network protocol or NULL
 *
 * Remove rules from the IP table context to stop masquerading
 * network @network on @physdev. This stops the bridge from
 * masquerading for that network (on @physdev).
 *
 * Returns 0 in case of success or an error code otherwise
 */
int
iptablesRemoveForwardMasquerade(virFirewallPtr fw,
                                virSocketAddr *netaddr,
                                unsigned int prefix,
                                const char *physdev,
                                virSocketAddrRangePtr addr,
                                virPortRangePtr port,
                                const char *protocol)
{
    return iptablesForwardMasquerade(fw, VIR_IPTABLES_CHAIN_BUILTIN, netaddr, prefix,
                                     physdev, addr, port, protocol, REMOVE);
}


/* Don't masquerade traffic coming from the network associated with the bridge
 * if said traffic targets @destaddr.
 */
static int
iptablesForwardDontMasquerade(virFirewallPtr fw,
                              int chain,
                              virSocketAddr *netaddr,
                              unsigned int prefix,
                              const char *physdev,
                              const char *destaddr,
                              int action)
{
    VIR_AUTOFREE(char *) networkstr = NULL;
    static const char *chainName[VIR_IPTABLES_CHAIN_LAST] = {
        "POSTROUTING",
        "PRT_libvirt",
    };

    if (!(networkstr = iptablesFormatNetwork(netaddr, prefix)))
        return -1;

    if (!VIR_SOCKET_ADDR_IS_FAMILY(netaddr, AF_INET)) {
        /* Higher level code *should* guaranteee it's impossible to get here. */
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Attempted to NAT '%s'. NAT is only supported for IPv4."),
                       networkstr);
        return -1;
    }

    if (physdev && physdev[0])
        virFirewallAddRule(fw, VIR_FIREWALL_LAYER_IPV4,
                           "--table", "nat",
                           action == ADD ? "--insert" : "--delete", chainName[chain],
                           "--out-interface", physdev,
                           "--source", networkstr,
                           "--destination", destaddr,
                           "--jump", "RETURN",
                           NULL);
    else
        virFirewallAddRule(fw, VIR_FIREWALL_LAYER_IPV4,
                           "--table", "nat",
                           action == ADD ? "--insert" : "--delete", chainName[chain],
                           "--source", networkstr,
                           "--destination", destaddr,
                           "--jump", "RETURN",
                           NULL);

    return 0;
}

/**
 * iptablesAddDontMasquerade:
 * @netaddr: the source network name
 * @prefix: prefix (# of 1 bits) of netmask to apply to @netaddr
 * @physdev: the physical output device or NULL
 * @destaddr: the destination network not to masquerade for
 *
 * Add rules to the IP table context to avoid masquerading from
 * @netaddr/@prefix to @destaddr on @physdev. @destaddr must be in a format
 * directly consumable by iptables, it must not depend on user input or
 * configuration.
 *
 * Returns 0 in case of success or an error code otherwise.
 */
int
iptablesAddDontMasquerade(virFirewallPtr fw,
                          virSocketAddr *netaddr,
                          unsigned int prefix,
                          const char *physdev,
                          const char *destaddr)
{
    return iptablesForwardDontMasquerade(fw, VIR_IPTABLES_CHAIN_BUILTIN, netaddr, prefix,
                                         physdev, destaddr, ADD);
}

/**
 * iptablesRemoveDontMasquerade:
 * @netaddr: the source network name
 * @prefix: prefix (# of 1 bits) of netmask to apply to @netaddr
 * @physdev: the physical output device or NULL
 * @destaddr: the destination network not to masquerade for
 *
 * Remove rules from the IP table context that prevent masquerading from
 * @netaddr/@prefix to @destaddr on @physdev. @destaddr must be in a format
 * directly consumable by iptables, it must not depend on user input or
 * configuration.
 *
 * Returns 0 in case of success or an error code otherwise.
 */
int
iptablesRemoveDontMasquerade(virFirewallPtr fw,
                             virSocketAddr *netaddr,
                             unsigned int prefix,
                             const char *physdev,
                             const char *destaddr)
{
    return iptablesForwardDontMasquerade(fw, VIR_IPTABLES_CHAIN_BUILTIN, netaddr, prefix,
                                         physdev, destaddr, REMOVE);
}


static void
iptablesOutputFixUdpChecksum(virFirewallPtr fw,
                             int chain,
                             const char *iface,
                             int port,
                             int action)
{
    char portstr[32];
    static const char *chainName[VIR_IPTABLES_CHAIN_LAST] = {
        "POSTROUTING",
        "PRT_libvirt",
    };

    snprintf(portstr, sizeof(portstr), "%d", port);
    portstr[sizeof(portstr) - 1] = '\0';

    virFirewallAddRule(fw, VIR_FIREWALL_LAYER_IPV4,
                       "--table", "mangle",
                       action == ADD ? "--insert" : "--delete", chainName[chain],
                       "--out-interface", iface,
                       "--protocol", "udp",
                       "--destination-port", portstr,
                       "--jump", "CHECKSUM", "--checksum-fill",
                       NULL);
}

/**
 * iptablesAddOutputFixUdpChecksum:
 * @ctx: pointer to the IP table context
 * @iface: the interface name
 * @port: the UDP port to match
 *
 * Add a rule to the mangle table's POSTROUTING chain that fixes up the
 * checksum of packets with the given destination @port.
 * the given @iface interface for TCP packets.
 *
 */
void
iptablesAddOutputFixUdpChecksum(virFirewallPtr fw,
                                const char *iface,
                                int port)
{
    iptablesOutputFixUdpChecksum(fw, VIR_IPTABLES_CHAIN_BUILTIN, iface, port, ADD);
}

/**
 * iptablesRemoveOutputFixUdpChecksum:
 * @ctx: pointer to the IP table context
 * @iface: the interface name
 * @port: the UDP port of the rule to remove
 *
 * Removes the checksum fixup rule that was previous added with
 * iptablesAddOutputFixUdpChecksum.
 */
void
iptablesRemoveOutputFixUdpChecksum(virFirewallPtr fw,
                                   const char *iface,
                                   int port)
{
    iptablesOutputFixUdpChecksum(fw, VIR_IPTABLES_CHAIN_BUILTIN, iface, port, REMOVE);
}
