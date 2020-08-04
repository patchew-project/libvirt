/*
 * virhostmem.h: helper APIs for host memory info
 *
 * Copyright (C) 2006-2016 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

int virHostMemGetStats(int cellNum,
                       virNodeMemoryStatsPtr params,
                       int *nparams,
                       unsigned int flags);
int virHostMemGetCellsFree(unsigned long long *freeMems,
                           int startCell,
                           int maxCells);
int virHostMemGetInfo(unsigned long long *mem,
                      unsigned long long *freeMem);

int virHostMemGetParameters(virTypedParameterPtr params,
                            int *nparams,
                            unsigned int flags);

int virHostMemSetParameters(virTypedParameterPtr params,
                            int nparams,
                            unsigned int flags);

int virHostMemGetFreePages(unsigned int npages,
                           unsigned int *pages,
                           int startCell,
                           unsigned int cellCount,
                           unsigned long long *counts);

int virHostMemAllocPages(unsigned int npages,
                         unsigned int *pageSizes,
                         unsigned long long *pageCounts,
                         int startCell,
                         unsigned int cellCount,
                         bool add);
