/*
 * hyperv_private.h: private driver struct for the Microsoft Hyper-V driver
 *
 * Copyright (C) 2011 Matthias Bolte <matthias.bolte@googlemail.com>
 * Copyright (C) 2009 Michael Sievers <msievers83@googlemail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "virerror.h"
#include "hyperv_util.h"
#include "openwsman.h"

typedef enum _hypervWmiVersion hypervWmiVersion;
enum _hypervWmiVersion {
    HYPERV_WMI_VERSION_V1,
    HYPERV_WMI_VERSION_V2,
};

typedef struct _hypervPrivate hypervPrivate;
struct _hypervPrivate {
    hypervParsedUri *parsedUri;
    WsManClient *client;
    hypervWmiVersion wmiVersion;
};
