/*
 * hyperv_wmi_classes.c: WMI classes for managing Microsoft Hyper-V hosts
 *
 * Copyright (C) 2011 Matthias Bolte <matthias.bolte@googlemail.com>
 * Copyright (C) 2009 Michael Sievers <msievers83@googlemail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <config.h>

#include "hyperv_wmi_classes.h"

SER_TYPEINFO_BOOL;
SER_TYPEINFO_STRING;
SER_TYPEINFO_INT8;
SER_TYPEINFO_INT16;
SER_TYPEINFO_INT32;
SER_TYPEINFO_UINT8;
SER_TYPEINFO_UINT16;
SER_TYPEINFO_UINT32;

#include "hyperv_wmi_classes.generated.c"
