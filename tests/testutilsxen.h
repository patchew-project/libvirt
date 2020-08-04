/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "capabilities.h"
#ifdef WITH_LIBXL
# include "libxl/libxl_conf.h"

libxlDriverPrivatePtr testXLInitDriver(void);

void testXLFreeDriver(libxlDriverPrivatePtr driver);

#endif /* WITH_LIBXL */
