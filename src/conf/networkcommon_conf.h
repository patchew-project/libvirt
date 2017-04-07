/*
 * networkcommon_conf.h: network XML handling
 *
 * Copyright (C) 2006-2014 Red Hat, Inc.
 * Copyright (C) 2006-2008 Daniel P. Berrange
 * Copyright (c) 2015 SUSE LINUX Products GmbH, Nuernberg, Germany.
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
 * Author: Daniel P. Berrange <berrange@redhat.com>
 */

#ifndef __NETWORKCOMMON_CONF_H__
# define __NETWORKCOMMON_CONF_H__

# include <libxml/tree.h>
# include <libxml/xpath.h>

# include "internal.h"
# include "virbuffer.h"
# include "virsocketaddr.h"
# include "virnetdevip.h"
# include "virnetdev.h"

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

virNetDevCoalescePtr
virNetDevCoalesceParseXML(xmlNodePtr node,
                          xmlXPathContextPtr ctxt);

void
virNetDevCoalesceFormatXML(virBufferPtr buf,
                           virNetDevCoalescePtr coalesce);

#endif /* __NETWORKCOMMON_CONF_H__ */
