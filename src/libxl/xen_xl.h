/*
 * xen_xl.h: Xen XL parsing functions
 *
 * Copyright (c) 2015 SUSE LINUX Products GmbH, Nuernberg, Germany.
 * Copyright (c) 2014 David Kiarie Kahurani
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "virconf.h"
#include "domain_conf.h"
#include "xen_common.h"

virDomainDefPtr xenParseXL(virConfPtr conn,
                           virCapsPtr caps,
                           virDomainXMLOptionPtr xmlopt);

virConfPtr xenFormatXL(virDomainDefPtr def, virConnectPtr);

const char *xenTranslateCPUFeature(const char *feature_name, bool from_libxl);
