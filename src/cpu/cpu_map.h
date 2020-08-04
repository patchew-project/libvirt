/*
 * cpu_map.h: internal functions for handling CPU mapping configuration
 *
 * Copyright (C) 2009 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virxml.h"

typedef int
(*cpuMapLoadCallback)  (xmlXPathContextPtr ctxt,
                        const char *name,
                        void *data);

int
cpuMapLoad(const char *arch,
           cpuMapLoadCallback vendorCB,
           cpuMapLoadCallback featureCB,
           cpuMapLoadCallback modelCB,
           void *data);
