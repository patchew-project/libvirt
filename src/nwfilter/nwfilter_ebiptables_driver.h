/*
 * nwfilter_ebiptables_driver.h: ebtables/iptables driver support
 *
 * Copyright (C) 2010 IBM Corporation
 * Copyright (C) 2010 Stefan Berger
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "nwfilter_tech_driver.h"

#define MAX_CHAINNAME_LENGTH  32 /* see linux/netfilter_bridge/ebtables.h */

extern virNWFilterTechDriver ebiptables_driver;

#define EBIPTABLES_DRIVER_ID "ebiptables"

#define IPTABLES_MAX_COMMENT_LENGTH  256
