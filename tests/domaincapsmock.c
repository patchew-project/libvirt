/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "virhostcpu.h"
#ifdef WITH_LIBXL
# include "libxl/libxl_capabilities.h"
#endif

#ifdef WITH_LIBXL
bool
libxlCapsHasPVUSB(void)
{
    return true;
}
#endif

int
virHostCPUGetKVMMaxVCPUs(void)
{
    return INT_MAX;
}
