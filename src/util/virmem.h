/*
 * virmem.h: Memory definitions and helpers
 *
 * Copyright (C) 2018 Red Hat, Inc.
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
 *
 * Author: Fabiano Fidêncio <fidencio@redhat.com>
 */

#ifndef __VIR_MEM_H__
# define __VIR_MEM_H__

# include "virutil.h"

typedef struct _virMemHugePage virMemHugePage;
typedef virMemHugePage *virMemHugePagePtr;
struct _virMemHugePage {
    virBitmapPtr nodemask;      /* guest's NUMA node mask */
    unsigned long long size;    /* hugepage size in KiB */
};

typedef struct _virMemTune virMemTune;
typedef virMemTune *virMemTunePtr;
struct _virMemTune {
    /* total memory size including memory modules in kibibytes, this field
     * should be accessed only via accessors */
    unsigned long long total_memory;
    unsigned long long cur_balloon; /* in kibibytes, capped at ulong thanks
                                       to virDomainGetInfo */

    virMemHugePagePtr hugepages;
    size_t nhugepages;

    /* maximum supported memory for a guest, for hotplugging */
    unsigned long long max_memory; /* in kibibytes */
    unsigned int memory_slots; /* maximum count of RAM memory slots */

    bool nosharepages;
    bool locked;
    int dump_core; /* enum virTristateSwitch */
    unsigned long long hard_limit; /* in kibibytes, limit at off_t bytes */
    unsigned long long soft_limit; /* in kibibytes, limit at off_t bytes */
    unsigned long long min_guarantee; /* in kibibytes, limit at off_t bytes */
    unsigned long long swap_hard_limit; /* in kibibytes, limit at off_t bytes */

    int source; /* enum virDomainMemorySource */
    int access; /* enum virDomainMemoryAccess */
    int allocation; /* enum virDomainMemoryAllocation */

    virTristateBool discard;
};

#endif /* __VIR_MEM_H__ */
