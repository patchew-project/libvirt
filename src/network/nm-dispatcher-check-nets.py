#!/usr/bin/env python3
#
# Copyright (C) 2012-2019 Red Hat, Inc.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library.  If not, see
# <http://www.gnu.org/licenses/>.

import libvirt
import sys
import os
import libxml2
from ipaddress import ip_network

# This script should be installed in
# /usr/lib/NetworkManager/dispatcher.d/50-libvirt-check-nets. It will be
# called by NetworkManager every time a network interface is taken up
# or down. When a new network comes up, it checks the libvirt virtual
# networks to see if their IP address(es) (including any static
# routes) are in conflict with the IP address(es) (or static routes)
# of the newly added interface.  If so, the libvirt network is
# disabled. It is assumed that the user will notice that their guests
# no longer have network connectvity (and/or the message logged by
# this script), see that the network has been disabled, and then
# realize the conflict when they try to restart it.
#
# set checkDefaultOnly=False to check *all* active libvirt networks
# for conflicts with the new interface. Set to True to check only the
# libvirt default network (since most networks other than the default
# network are added post-install at a time when all of the hosts other
# networks are already active, it may be overkill to check all of the
# libvirt networks for conflict here (and instead just add more
# needless overheard to bringing up a new host interface).
#
checkDefaultOnly = False

# NB: since this file is installed in /usr/lib, it really shouldn't be
# modified by the user, but instead should be copied to
# /etc/NetworkManager/dispatcher.d, where it will override the copy in
# /usr/lib. Even that isn't a proper solution though - if we're going
# to actually have this config knob, perhaps we should check for it in
# the environment, and if someone wants to modify it they can put a
# short script in /etc that exports and environment variable and then
# calls this script? Just thinking out loud here...

def checkconflict(conn, netname, hostnets, hostif):

    # ignore if the network has been brought down or removed since we
    # got the list
    try:
        net = conn.networkLookupByName(netname)
    except libvirt.libvirtError:
        return

    if not net.isActive():
        return

    xml = net.XMLDesc()
    doc = libxml2.parseDoc(xml)
    ctx = doc.xpathNewContext()

    # see if NetworkManager is informing us that this libvirt network
    # itself is coming online
    bridge = ctx.xpathEval("/network/bridge/@name");
    if bridge and bridge[0].content == hostif:
        return

    # check *all* the addresses of this network
    addrs = ctx.xpathEval("/network/*[@address]")
    for ip in addrs:
        ctx.setContextNode(ip)
        address = ctx.xpathEval("@address")
        prefix = ctx.xpathEval("@prefix")
        netmask = ctx.xpathEval("@netmask")

        if not (address and len(address[0].content)):
            continue

        addrstr = address[0].content
        if not (prefix and len(prefix[0].content)):
            # check for a netmask
            if not (netmask and len(netmask[0].content)):
                # this element has address, but no prefix or netmask
                # probably it is <mac address ...> so we can ignore it
                continue
            # convert netmask to prefix
            prefixstr = str(ip_network("0.0.0.0/%s" % netmask[0].content).prefixlen)
        else:
            prefixstr = prefix[0].content

        virtnetaddress = ip_network("%s/%s" % (addrstr, prefixstr), strict = False)
        # print("network %s address %s" % (netname, str(virtnetaddress)))
        for hostnet in hostnets:
            if virtnetaddress == hostnet:
                # There is a conflict with this libvirt network and the specified
                # net, so we need to disable the libvirt network
                print("Conflict with host net %s when starting interface %s - bringing down libvirt network '%s'"
                      % (str(hostnet), hostif, netname))
                try:
                    net.destroy()
                except libvirt.libvirtError:
                    print("Failed to destroy network %s" % netname)
                return
    return


def addHostNets(hostnets, countenv, addrenv):

    count = os.getenv(countenv)
    if not count or count == 0:
        return

    for num in range(int(count)):
        addrstr = os.getenv("%s_%d" % (addrenv, num))
        if not addrstr or addrstr == "":
            continue

        net = ip_network(addrstr.split()[0], strict=False)
        if net:
            hostnets.append(net)
    return


############################################################

if sys.argv[2] != "up":
    sys.exit(0)

hostif = sys.argv[1]

try:
    conn = libvirt.open(None)
except libvirt.libvirtError:
    print('Failed to open connection to the hypervisor')
    sys.exit(0)

if checkDefaultOnly:
    nets = []
    net = conn.networkLookupByName("default")
    if not (net and net.isActive()):
        sys.exit(0)
    nets.append(net)
else:
    nets = conn.listAllNetworks(libvirt.VIR_CONNECT_LIST_NETWORKS_ACTIVE)
    if not nets:
        sys.exit(0)

# We have at least one active network. Build a list of all network
# routes added by the new interface, and compare that list to the list
# of all networks used by each active libvirt network. If any are an
# exact match, then we have a conflict and need to shut down the
# libvirt network to avoid killing host networking.

# When NetworkManager calls scripts in /etc/NetworkManager/dispatcher.d
# it will have all IP addresses and routes associated with the interface
# that is going up or down in the following environment variables:
#
# IP4_NUM_ADDRESSES - number of IPv4 addresses
# IP4_ADDRESS_N - one variable for each address, starting at _0
# IP4_NUM_ROUTES - number of IPv5 routes
# IP4_ROUTE_N - one for each route, starting at _0
# (replace "IP4" with "IP6" and repeat)
#
hostnets = []
addHostNets(hostnets, "IP4_NUM_ADDRESSES", "IP4_ADDRESS");
addHostNets(hostnets, "IP4_NUM_ROUTES", "IP4_ROUTE");
addHostNets(hostnets, "IP6_NUM_ADDRESSES", "IP6_ADDRESS");
addHostNets(hostnets, "IP6_NUM_ROUTES", "IP6_ROUTE");

for net in nets:

    checkconflict(conn, net.name(), hostnets, hostif)
