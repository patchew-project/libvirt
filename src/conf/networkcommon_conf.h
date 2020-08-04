/*
 * networkcommon_conf.h: network XML handling
 *
 * Copyright (C) 2006-2014 Red Hat, Inc.
 * Copyright (C) 2006-2008 Daniel P. Berrange
 * Copyright (c) 2015 SUSE LINUX Products GmbH, Nuernberg, Germany.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <libxml/tree.h>
#include <libxml/xpath.h>

#include "internal.h"
#include "virbuffer.h"
#include "virsocketaddr.h"
#include "virnetdevip.h"

virNetDevIPRoutePtr
virNetDevIPRouteCreate(const char *networkName,
                       const char *family,
                       const char *address,
                       const char *netmask,
                       const char *gateway,
                       unsigned int prefix,
                       bool hasPrefix,
                       unsigned int metric,
                       bool hasMetric);

virNetDevIPRoutePtr
virNetDevIPRouteParseXML(const char *networkName,
                         xmlNodePtr node,
                         xmlXPathContextPtr ctxt);
int
virNetDevIPRouteFormat(virBufferPtr buf,
                       const virNetDevIPRoute *def);
