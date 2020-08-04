/*
 * Copyright (C) 2009-2011 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virnetdevbandwidth.h"
#include "virbuffer.h"
#include "virxml.h"
#include "domain_conf.h"
#include "network_conf.h"

int virNetDevBandwidthParse(virNetDevBandwidthPtr *bandwidth,
                            unsigned int *class_id,
                            xmlNodePtr node,
                            bool allowFloor)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;
int virNetDevBandwidthFormat(const virNetDevBandwidth *def,
                             unsigned int class_id,
                             virBufferPtr buf);

void virDomainClearNetBandwidth(virDomainDefPtr def)
    ATTRIBUTE_NONNULL(1);

bool virNetDevSupportsBandwidth(virDomainNetType type);
bool virNetDevBandwidthHasFloor(const virNetDevBandwidth *b);
bool virNetDevBandwidthSupportsFloor(virNetworkForwardType type);
