/*
 * virresctrl.h: header for managing resctrl control
 *
 * Copyright (C) 2016 Intel, Inc.
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
 * Authors:
 * Eli Qiao <liyong.qiao@intel.com>
 */

#ifndef __VIR_RESCTRL_H__
# define __VIR_RESCTRL_H__

typedef enum {
    VIR_RESCTRL_TYPE_L3,
    VIR_RESCTRL_TYPE_L3_CODE,
    VIR_RESCTRL_TYPE_L3_DATA,
    VIR_RESCTRL_TYPE_L2,

    VIR_RESCTRL_TYPE_LAST
} virResctrlType;

VIR_ENUM_DECL(virResctrl);

/*
 * a virResctrlSchemataItem represents one of schemata object in a
 * resource control group.
 * eg: 0=f
 */
typedef struct _virResCtrlSchemataItem virResctrlSchemataItem;
typedef virResctrlSchemataItem *virResctrlSchemataItemPtr;
struct _virResctrlSchemataItem {
    unsigned int cache_id; /* cache resource id, eg: 0 */
    unsigned int continuous_schemata; /* schemata, should be a continuous bits,
                                         eg: f, this schemata can be persisted
                                         to sysfs */
    unsigned int schemata; /* schemata eg: f0f, a schemata which is calculated
                              at running time */
    unsigned long long size; /* the cache size schemata represented in B,
                              eg: (min * bits of continuous_schemata) */
};

/*
 * a virResctrlSchemata represents schemata objects of specific type of
 * resource in a resource control group.
 * eg: L3:0=f,1=ff
 */
typedef struct _virResctrlSchemata virResctrlSchemata;
typedef virResctrlSchemata *virResctrlSchemataPtr;
struct _virResctrlSchemata {
    virResctrlType type; /* resource control type, eg: L3 */
    size_t n_schemata_items; /* number of schemata item, eg: 2 */
    virResctrlSchemataItemPtr *schemata_items; /* pointer list of schemata item */
};

/* Get free cache of the host, result saved in schemata */
int virResctrlGetFreeCache(virResctrlType type,
                           virResctrlSchemataPtr *schemata);

/* Get free cache of specific cache id of the host, result saved in
   schemataitem */
int virResctrlGetFreeCacheByCacheId(virResctrlType type,
                                    virResctrlSchemataItemPtr *schemataitem);

/* Set cache allocation for a VM domain */
int virResctrlSetCacheBanks(virDomainCachetunePtr cachetune,
                            unsigned char *group_name,
                            size_t n_pids,
                            pid_t *pids);

/* remove cache allocation for a VM domain */
int virResctrlRemoveCacheBanks(unsigned char *group_name);
#endif
