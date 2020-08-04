/*
 * virnuma.h: helper APIs for managing numa
 *
 * Copyright (C) 2011-2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "virbitmap.h"


char *virNumaGetAutoPlacementAdvice(unsigned short vcpus,
                                    unsigned long long balloon);

int virNumaSetupMemoryPolicy(virDomainNumatuneMemMode mode,
                             virBitmapPtr nodeset);

virBitmapPtr virNumaGetHostMemoryNodeset(void);
bool virNumaNodesetIsAvailable(virBitmapPtr nodeset) G_GNUC_NO_INLINE;
bool virNumaIsAvailable(void) G_GNUC_NO_INLINE;
int virNumaGetMaxNode(void) G_GNUC_NO_INLINE;
bool virNumaNodeIsAvailable(int node) G_GNUC_NO_INLINE;
int virNumaGetDistances(int node,
                        int **distances,
                        int *ndistances) G_GNUC_NO_INLINE;
int virNumaGetNodeMemory(int node,
                         unsigned long long *memsize,
                         unsigned long long *memfree) G_GNUC_NO_INLINE;

unsigned int virNumaGetMaxCPUs(void);

int virNumaGetNodeCPUs(int node, virBitmapPtr *cpus) G_GNUC_NO_INLINE;
int virNumaNodesetToCPUset(virBitmapPtr nodeset,
                           virBitmapPtr *cpuset);

int virNumaGetPageInfo(int node,
                       unsigned int page_size,
                       unsigned long long huge_page_sum,
                       unsigned long long *page_avail,
                       unsigned long long *page_free);
int virNumaGetPages(int node,
                    unsigned int **pages_size,
                    unsigned long long **pages_avail,
                    unsigned long long **pages_free,
                    size_t *npages)
    ATTRIBUTE_NONNULL(5) G_GNUC_NO_INLINE;
int virNumaSetPagePoolSize(int node,
                           unsigned int page_size,
                           unsigned long long page_count,
                           bool add);
