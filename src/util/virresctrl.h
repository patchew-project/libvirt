/*
 *  * virrscctrl.h: methods for managing rscctrl
 *  *
 *  * Copyright (C) 2016 Intel, Inc.
 *  *
 *  * This library is free software; you can redistribute it and/or
 *  * modify it under the terms of the GNU Lesser General Public
 *  * License as published by the Free Software Foundation; either
 *  * version 2.1 of the License, or (at your option) any later version.
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

# include "virutil.h"
# include "virbitmap.h"
# include "domain_conf.h"

#define RESCTRL_DIR "/sys/fs/resctrl"
#define RESCTRL_INFO_DIR "/sys/fs/resctrl/info"
#define SYSFS_SYSTEM_PATH "/sys/devices/system"

#define MAX_CPU_SOCKET_NUM 8
#define MAX_CBM_BIT_LEN 32
#define MAX_SCHEMATA_LEN 1024
#define MAX_FILE_LEN ( 10 * 1024 * 1024)

enum {
    RDT_RESOURCE_L3,
    RDT_RESOURCE_L3DATA,
    RDT_RESOURCE_L3CODE,
    RDT_RESOURCE_L2,
    /* Must be the last */
    RDT_NUM_RESOURCES
};

VIR_ENUM_DECL(virResCtrl);

typedef struct _virResCacheBank virResCacheBank;
typedef virResCacheBank *virResCacheBankPtr;
struct _virResCacheBank {
    unsigned int host_id;
    unsigned long long cache_size;
    unsigned long long cache_left;
    unsigned long long cache_min;
    virBitmapPtr cpu_mask;
};

/**
 * struct rdt_resource - attributes of an RDT resource
 * @enabled:                    Is this feature enabled on this machine
 * @name:                       Name to use in "schemata" file
 * @num_closid:                 Number of CLOSIDs available
 * @max_cbm:                    Largest Cache Bit Mask allowed
 * @min_cbm_bits:               Minimum number of consecutive bits to be set
 *                              in a cache bit mask
 * @cache_level:                Which cache level defines scope of this domain
 * @num_banks:                  Number of cache bank on this machine.
 * @cache_banks:                Array of cache bank
 */
typedef struct _virResCtrl virResCtrl;
typedef virResCtrl *virResCtrlPtr;
struct _virResCtrl {
        bool                    enabled;
        const char              *name;
        int                     num_closid;
        int                     cbm_len;
        int                     min_cbm_bits;
        const char*             cache_level;
        int                     num_banks;
        virResCacheBankPtr      cache_banks;
};

/**
 * a virResSchemata represents a schemata object under a resource control
 * domain.
 */
typedef struct _virResSchemataItem virResSchemataItem;
typedef virResSchemataItem *virResSchemataItemPtr;
struct _virResSchemataItem {
    unsigned int socket_no;
    unsigned schemata;
};

typedef struct _virResSchemata virResSchemata;
typedef virResSchemata *virResSchemataPtr;
struct _virResSchemata {
    unsigned int n_schemata_items;
    virResSchemataItemPtr schemata_items;
};

/**
 * a virResDomain represents a resource control domain. It's a double linked
 * list.
 */

typedef struct _virResDomain virResDomain;
typedef virResDomain *virResDomainPtr;

struct _virResDomain {
    char* name;
    virResSchemataPtr schematas[RDT_NUM_RESOURCES];
    char* tasks;
    int n_sockets;
    virResDomainPtr pre;
    virResDomainPtr next;
};

/* All resource control domains on this host*/

typedef struct _virResCtrlDomain virResCtrlDomain;
typedef virResCtrlDomain *virResCtrlDomainPtr;
struct _virResCtrlDomain {
    unsigned int num_domains;
    virResDomainPtr domains;
};

bool virResCtrlAvailable(void);
int virResCtrlInit(void);
virResCtrlPtr virResCtrlGet(int);
int virResCtrlSetCacheBanks(virDomainCachetunePtr, unsigned char*, pid_t);
int virResCtrlUpdate(void);
#endif
