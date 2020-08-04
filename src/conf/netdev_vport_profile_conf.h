/*
 * Copyright (C) 2009-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virnetdevvportprofile.h"
#include "virbuffer.h"
#include "virxml.h"

typedef enum {
    /* generate random defaults for interfaceID/interfaceID
     * when appropriate
     */
    VIR_VPORT_XML_GENERATE_MISSING_DEFAULTS = (1<<0),
    /* fail if any attribute required for the specified
     * type is missing
     */
    VIR_VPORT_XML_REQUIRE_ALL_ATTRIBUTES    = (1<<1),
    /* fail if no type is specified */
    VIR_VPORT_XML_REQUIRE_TYPE              = (1<<2),
} virNetDevVPortXMLFlags;

virNetDevVPortProfilePtr
virNetDevVPortProfileParse(xmlNodePtr node, unsigned int flags);

int
virNetDevVPortProfileFormat(const virNetDevVPortProfile *virtPort,
                            virBufferPtr buf);
