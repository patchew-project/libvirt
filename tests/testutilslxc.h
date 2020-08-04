/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "capabilities.h"

#include "lxc/lxc_conf.h"

#define FAKEDEVDIR0 "/fakedevdir0/bla/fasl"
#define FAKEDEVDIR1 "/fakedevdir1/bla/fasl"

virCapsPtr testLXCCapsInit(void);
virLXCDriverPtr testLXCDriverInit(void);
void testLXCDriverFree(virLXCDriverPtr driver);
