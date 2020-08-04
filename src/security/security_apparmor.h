/*
 * Copyright (C) 2009 Canonical Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "security_driver.h"

extern virSecurityDriver virAppArmorSecurityDriver;

#define AA_PREFIX  "libvirt-"
#define PROFILE_NAME_SIZE  8 + VIR_UUID_STRING_BUFLEN /* AA_PREFIX + uuid */
#define MAX_FILE_LEN       (1024*1024*10)  /* 10MB limit for sanity check */
