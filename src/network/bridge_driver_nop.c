/*
 * Copyright (C) 2006-2013 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

void networkPreReloadFirewallRules(virNetworkDriverStatePtr driver G_GNUC_UNUSED,
                                   bool startup G_GNUC_UNUSED,
                                   bool force G_GNUC_UNUSED)
{
}


void networkPostReloadFirewallRules(bool startup G_GNUC_UNUSED)
{
}


int networkCheckRouteCollision(virNetworkDefPtr def G_GNUC_UNUSED)
{
    return 0;
}

int networkAddFirewallRules(virNetworkDefPtr def G_GNUC_UNUSED)
{
    return 0;
}

void networkRemoveFirewallRules(virNetworkDefPtr def G_GNUC_UNUSED)
{
}
