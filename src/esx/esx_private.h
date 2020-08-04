/*
 * esx_private.h: private driver struct for the VMware ESX driver
 *
 * Copyright (C) 2009-2011 Matthias Bolte <matthias.bolte@googlemail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "virerror.h"
#include "capabilities.h"
#include "domain_conf.h"
#include "esx_vi.h"

typedef struct _esxPrivate {
    esxVI_Context *primary; /* points to host or vCenter */
    esxVI_Context *host;
    esxVI_Context *vCenter;
    esxUtil_ParsedUri *parsedUri;
    virCapsPtr caps;
    virDomainXMLOptionPtr xmlopt;
    int32_t maxVcpus;
    esxVI_Boolean supportsVMotion;
    esxVI_Boolean supportsLongMode; /* aka x86_64 */
    esxVI_Boolean supportsScreenshot;
    int32_t usedCpuTimeCounterId;
} esxPrivate;
