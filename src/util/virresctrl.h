/*
 * virresctrl.h:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef __VIR_RESCTRL_H__
# define __VIR_RESCTRL_H__

# include "internal.h"

# include "virbitmap.h"
# include "virutil.h"


typedef enum {
    VIR_CACHE_TYPE_BOTH,
    VIR_CACHE_TYPE_CODE,
    VIR_CACHE_TYPE_DATA,

    VIR_CACHE_TYPE_LAST
} virCacheType;

VIR_ENUM_DECL(virCache);

typedef struct _virCacheInfo virCacheInfo;
typedef virCacheInfo *virCacheInfoPtr;

virCacheInfoPtr
virCacheInfoNew(void);

typedef int virCacheForeachControlCallback(unsigned long long granularity,
                                           unsigned long long min,
                                           virCacheType scope,
                                           unsigned int max_allocations,
                                           void *opaque);


int
virCacheControlForeach(virCacheInfoPtr ci,
                       unsigned int level,
                       virCacheType type,
                       unsigned int id,
                       virCacheForeachControlCallback cb,
                       void *opaque);


typedef int virCacheForeachBankCallback(virCacheInfoPtr ci,
                                        unsigned int level,
                                        virCacheType type,
                                        unsigned int id,
                                        unsigned long long size,
                                        virBitmapPtr cpus,
                                        void *opaque);


int
virCacheForeachBank(virCacheInfoPtr ci,
                    virCacheForeachBankCallback cb,
                    void *opaque);


/* Alloc-related things */
typedef struct _virResctrlAlloc virResctrlAlloc;
typedef virResctrlAlloc *virResctrlAllocPtr;

typedef int virResctrlAllocForeachSizeCallback(unsigned int level,
                                               virCacheType type,
                                               unsigned int cache,
                                               unsigned long long size,
                                               void *opaque);

virResctrlAllocPtr
virResctrlAllocNew(void);

bool
virResctrlAllocIsEmpty(virResctrlAllocPtr alloc);

int
virResctrlAllocSetSize(virResctrlAllocPtr alloc,
                       unsigned int level,
                       virCacheType type,
                       unsigned int cache,
                       unsigned long long size);

int
virResctrlAllocForeachSize(virResctrlAllocPtr alloc,
                           virResctrlAllocForeachSizeCallback cb,
                           void *opaque);

int
virResctrlAllocSetID(virResctrlAllocPtr alloc,
                     const char *id);
const char *
virResctrlAllocGetID(virResctrlAllocPtr alloc);

char *
virResctrlAllocFormat(virResctrlAllocPtr alloc);

int
virResctrlAllocDeterminePath(virResctrlAllocPtr alloc,
                             const char *machinename);

int
virResctrlAllocCreate(virResctrlAllocPtr alloc,
                      const char *machinename);

int
virResctrlAllocAddPID(virResctrlAllocPtr alloc,
                      pid_t pid);

int
virResctrlAllocRemove(virResctrlAllocPtr alloc);

#endif /*  __VIR_RESCTRL_H__ */
