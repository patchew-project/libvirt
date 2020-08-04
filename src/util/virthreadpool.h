/*
 * virthreadpool.h: a generic thread pool implementation
 *
 * Copyright (C) 2010 Hu Tao
 * Copyright (C) 2010 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

typedef struct _virThreadPool virThreadPool;
typedef virThreadPool *virThreadPoolPtr;

typedef void (*virThreadPoolJobFunc)(void *jobdata, void *opaque);

#define virThreadPoolNew(min, max, prio, func, opaque) \
    virThreadPoolNewFull(min, max, prio, func, #func, opaque)

virThreadPoolPtr virThreadPoolNewFull(size_t minWorkers,
                                      size_t maxWorkers,
                                      size_t prioWorkers,
                                      virThreadPoolJobFunc func,
                                      const char *name,
                                      void *opaque) ATTRIBUTE_NONNULL(4);

size_t virThreadPoolGetMinWorkers(virThreadPoolPtr pool);
size_t virThreadPoolGetMaxWorkers(virThreadPoolPtr pool);
size_t virThreadPoolGetPriorityWorkers(virThreadPoolPtr pool);
size_t virThreadPoolGetCurrentWorkers(virThreadPoolPtr pool);
size_t virThreadPoolGetFreeWorkers(virThreadPoolPtr pool);
size_t virThreadPoolGetJobQueueDepth(virThreadPoolPtr pool);

void virThreadPoolFree(virThreadPoolPtr pool);

int virThreadPoolSendJob(virThreadPoolPtr pool,
                         unsigned int priority,
                         void *jobdata) ATTRIBUTE_NONNULL(1)
                                        G_GNUC_WARN_UNUSED_RESULT;

int virThreadPoolSetParameters(virThreadPoolPtr pool,
                               long long int minWorkers,
                               long long int maxWorkers,
                               long long int prioWorkers);
