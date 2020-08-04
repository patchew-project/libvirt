/*
 * viriptables.h: helper APIs for managing iptables
 *
 * Copyright (C) 2007, 2008 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virsocketaddr.h"
#include "virfirewall.h"

int              iptablesSetupPrivateChains      (virFirewallLayer layer);

void             iptablesSetDeletePrivate        (bool pvt);

void             iptablesAddTcpInput             (virFirewallPtr fw,
                                                  virFirewallLayer layer,
                                                  const char *iface,
                                                  int port);
void             iptablesRemoveTcpInput          (virFirewallPtr fw,
                                                  virFirewallLayer layer,
                                                  const char *iface,
                                                  int port);

void             iptablesAddUdpInput             (virFirewallPtr fw,
                                                  virFirewallLayer layer,
                                                  const char *iface,
                                                  int port);
void             iptablesRemoveUdpInput          (virFirewallPtr fw,
                                                  virFirewallLayer layer,
                                                  const char *iface,
                                                  int port);

void             iptablesAddTcpOutput            (virFirewallPtr fw,
                                                  virFirewallLayer layer,
                                                  const char *iface,
                                                  int port);
void             iptablesRemoveTcpOutput         (virFirewallPtr fw,
                                                  virFirewallLayer layer,
                                                  const char *iface,
                                                  int port);
void             iptablesAddUdpOutput            (virFirewallPtr fw,
                                                  virFirewallLayer layer,
                                                  const char *iface,
                                                  int port);
void             iptablesRemoveUdpOutput         (virFirewallPtr fw,
                                                  virFirewallLayer layer,
                                                  const char *iface,
                                                  int port);

int              iptablesAddForwardAllowOut      (virFirewallPtr fw,
                                                  virSocketAddr *netaddr,
                                                  unsigned int prefix,
                                                  const char *iface,
                                                  const char *physdev)
    G_GNUC_WARN_UNUSED_RESULT;
int              iptablesRemoveForwardAllowOut   (virFirewallPtr fw,
                                                  virSocketAddr *netaddr,
                                                  unsigned int prefix,
                                                  const char *iface,
                                                  const char *physdev)
    G_GNUC_WARN_UNUSED_RESULT;
int              iptablesAddForwardAllowRelatedIn(virFirewallPtr fw,
                                                  virSocketAddr *netaddr,
                                                  unsigned int prefix,
                                                  const char *iface,
                                                  const char *physdev)
    G_GNUC_WARN_UNUSED_RESULT;
int              iptablesRemoveForwardAllowRelatedIn(virFirewallPtr fw,
                                                     virSocketAddr *netaddr,
                                                     unsigned int prefix,
                                                     const char *iface,
                                                     const char *physdev)
    G_GNUC_WARN_UNUSED_RESULT;

int              iptablesAddForwardAllowIn       (virFirewallPtr fw,
                                                  virSocketAddr *netaddr,
                                                  unsigned int prefix,
                                                  const char *iface,
                                                  const char *physdev)
    G_GNUC_WARN_UNUSED_RESULT;
int              iptablesRemoveForwardAllowIn    (virFirewallPtr fw,
                                                  virSocketAddr *netaddr,
                                                  unsigned int prefix,
                                                  const char *iface,
                                                  const char *physdev)
    G_GNUC_WARN_UNUSED_RESULT;

void             iptablesAddForwardAllowCross    (virFirewallPtr fw,
                                                  virFirewallLayer layer,
                                                  const char *iface);
void             iptablesRemoveForwardAllowCross (virFirewallPtr fw,
                                                  virFirewallLayer layer,
                                                  const char *iface);

void             iptablesAddForwardRejectOut     (virFirewallPtr fw,
                                                  virFirewallLayer layer,
                                                  const char *iface);
void             iptablesRemoveForwardRejectOut  (virFirewallPtr fw,
                                                  virFirewallLayer layer,
                                                  const char *iface);

void             iptablesAddForwardRejectIn      (virFirewallPtr fw,
                                                  virFirewallLayer layer,
                                                  const char *iface);
void             iptablesRemoveForwardRejectIn   (virFirewallPtr fw,
                                                  virFirewallLayer layery,
                                                  const char *iface);

int              iptablesAddForwardMasquerade    (virFirewallPtr fw,
                                                  virSocketAddr *netaddr,
                                                  unsigned int prefix,
                                                  const char *physdev,
                                                  virSocketAddrRangePtr addr,
                                                  virPortRangePtr port,
                                                  const char *protocol)
    G_GNUC_WARN_UNUSED_RESULT;
int              iptablesRemoveForwardMasquerade (virFirewallPtr fw,
                                                  virSocketAddr *netaddr,
                                                  unsigned int prefix,
                                                  const char *physdev,
                                                  virSocketAddrRangePtr addr,
                                                  virPortRangePtr port,
                                                  const char *protocol)
    G_GNUC_WARN_UNUSED_RESULT;
int              iptablesAddDontMasquerade       (virFirewallPtr fw,
                                                  virSocketAddr *netaddr,
                                                  unsigned int prefix,
                                                  const char *physdev,
                                                  const char *destaddr)
    G_GNUC_WARN_UNUSED_RESULT;
int              iptablesRemoveDontMasquerade    (virFirewallPtr fw,
                                                  virSocketAddr *netaddr,
                                                  unsigned int prefix,
                                                  const char *physdev,
                                                  const char *destaddr)
    G_GNUC_WARN_UNUSED_RESULT;
void             iptablesAddOutputFixUdpChecksum (virFirewallPtr fw,
                                                  const char *iface,
                                                  int port);
void             iptablesRemoveOutputFixUdpChecksum (virFirewallPtr fw,
                                                     const char *iface,
                                                     int port);
