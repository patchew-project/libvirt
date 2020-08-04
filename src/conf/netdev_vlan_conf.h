/*
 * Copyright (C) 2009-2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virnetdevvlan.h"
#include "virbuffer.h"
#include "virxml.h"

int virNetDevVlanParse(xmlNodePtr node, xmlXPathContextPtr ctxt, virNetDevVlanPtr def);
int virNetDevVlanFormat(const virNetDevVlan *def, virBufferPtr buf);
