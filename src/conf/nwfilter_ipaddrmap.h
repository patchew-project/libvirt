/*
 * nwfilter_ipaddrmap.h: IP address map for mapping interfaces to their
 *                       detected/expected IP addresses
 *
 * Copyright (C) 2010, 2012 IBM Corp.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

int virNWFilterIPAddrMapInit(void);
void virNWFilterIPAddrMapShutdown(void);

int virNWFilterIPAddrMapAddIPAddr(const char *ifname, char *addr);
int virNWFilterIPAddrMapDelIPAddr(const char *ifname,
                                  const char *ipaddr);
virNWFilterVarValuePtr virNWFilterIPAddrMapGetIPAddr(const char *ifname);
