/*
 * xen_xm.h: Xen XM parsing functions
 *
 * Copyright (C) 2011 Univention GmbH
 * Copyright (C) 2006-2007, 2009-2010 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virconf.h"
#include "domain_conf.h"

virConfPtr xenFormatXM(virConnectPtr conn, virDomainDefPtr def);

virDomainDefPtr xenParseXM(virConfPtr conf,
                           virCapsPtr caps, virDomainXMLOptionPtr xmlopt);
