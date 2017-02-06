/*
 * virresctrl.c: methods for managing resource contral
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
 *  Eli Qiao <liyong.qiao@intel.com>
 */
#include <config.h>

#include <sys/ioctl.h>
#if defined HAVE_SYS_SYSCALL_H
# include <sys/syscall.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "virresctrl.h"
#include "viralloc.h"
#include "virerror.h"
#include "virfile.h"
#include "virhostcpu.h"
#include "virlog.h"
#include "virstring.h"
#include "nodeinfo.h"

VIR_LOG_INIT("util.resctrl");

#define VIR_FROM_THIS VIR_FROM_RESCTRL

static unsigned int host_id = 0;

static virResCtrl ResCtrlAll[] = {
    {
        .name = "L3",
        .cache_level = "l3",
    },
    {
        .name = "L3DATA",
        .cache_level = "l3",
    },
    {
        .name = "L3CODE",
        .cache_level = "l3",
    },
    {
        .name = "L2",
        .cache_level = "l2",
    },
};

static int virResCtrlGetInfoStr(const int type, const char *item, char **str)
{
    int ret = 0;
    char *tmp;
    char *path;

    if (asprintf(&path, "%s/%s/%s", RESCTRL_INFO_DIR, ResCtrlAll[type].name, item) < 0)
        return -1;
    if (virFileReadAll(path, 10, str) < 0) {
        ret = -1;
        goto cleanup;
    }

    if ((tmp = strchr(*str, '\n'))) {
        *tmp = '\0';
    }

cleanup:
    VIR_FREE(path);
    return ret;
}


static int virResCtrlGetCPUValue(const char* path, char** value)
{
    int ret = -1;
    char* tmp;

    if(virFileReadAll(path, 10, value) < 0) {
        goto cleanup;
    }
    if ((tmp = strchr(*value, '\n'))) {
        *tmp = '\0';
    }
    ret = 0;
cleanup:
    return ret;
}

static int virResctrlGetCPUSocketID(const size_t cpu, int* socket_id)
{
    int ret = -1;
    char* physical_package_path = NULL;
    char* physical_package = NULL;
    if (virAsprintf(&physical_package_path,
                    "%s/cpu/cpu%zu/topology/physical_package_id",
                    SYSFS_SYSTEM_PATH, cpu) < 0) {
        return -1;
    }

    if(virResCtrlGetCPUValue(physical_package_path,
                             &physical_package) < 0)
        goto cleanup;

    if (virStrToLong_i(physical_package, NULL, 0, socket_id) < 0)
        goto cleanup;

    ret = 0;
cleanup:
    VIR_FREE(physical_package);
    VIR_FREE(physical_package_path);
    return ret;
}

static int virResCtrlGetCPUCache(const size_t cpu, int type, int *cache)
{
    int ret = -1;
    char* cache_dir = NULL;
    char* cache_str = NULL;
    char* tmp;
    int carry = -1;

    if (virAsprintf(&cache_dir,
                    "%s/cpu/cpu%zu/cache/index%d/size",
                    SYSFS_SYSTEM_PATH, cpu, type) < 0)
        return -1;

    if(virResCtrlGetCPUValue(cache_dir, &cache_str) < 0)
        goto cleanup;

    tmp = cache_str;

    while (*tmp != '\0')
        tmp++;
    if (*(tmp - 1) == 'K') {
        *(tmp - 1) = '\0';
        carry = 1;
    }
    else if (*(tmp - 1) == 'M') {
        *(tmp - 1) = '\0';
        carry = 1024;
    }

    if (virStrToLong_i(cache_str, NULL, 0, cache) < 0)
        goto cleanup;

    *cache = (*cache) * carry;

    if (*cache < 0)
        goto cleanup;

    ret = 0;
cleanup:
    VIR_FREE(cache_dir);
    VIR_FREE(cache_str);
    return ret;
}

/* Fill all cache bank informations */
static virResCacheBankPtr virResCtrlGetCacheBanks(int type, int* n_sockets)
{
    int npresent_cpus;
    int index = -1;
    virResCacheBankPtr bank;

    *n_sockets = 1;
    if ((npresent_cpus = virHostCPUGetCount()) < 0)
        return NULL;

    if (type == RDT_RESOURCE_L3
            || type == RDT_RESOURCE_L3DATA
            || type == RDT_RESOURCE_L3CODE)
        index = 3;
    else if (type == RDT_RESOURCE_L2) {
        index = 2;
    }

    if (index == -1)
        return NULL;

    if(VIR_ALLOC_N(bank, *n_sockets) < 0)
    {
        *n_sockets = 0;
        return NULL;
    }

    for( size_t i = 0; i < npresent_cpus ; i ++) {
        int s_id;
        int cache_size;

        if (virResctrlGetCPUSocketID(i, &s_id) < 0) {
            goto error;
        }

        if(s_id > (*n_sockets - 1)) {
            size_t cur = *n_sockets;
            size_t exp = s_id - (*n_sockets) + 1;
            if(VIR_EXPAND_N(bank, cur, exp) < 0) {
                goto error;
            }
        }
        *n_sockets = s_id + 1;
        if (bank[s_id].cpu_mask == NULL) {
            if (!(bank[s_id].cpu_mask = virBitmapNew(npresent_cpus)))
                goto error;
        }

        ignore_value(virBitmapSetBit(bank[s_id].cpu_mask, i));

        if (bank[s_id].cache_size == 0) {
           if (virResCtrlGetCPUCache(i, index, &cache_size) < 0) {
                goto error;
            }
            bank[s_id].cache_size = cache_size;
            bank[s_id].cache_min = cache_size / ResCtrlAll[type].cbm_len;
        }
    }
    return bank;

error:
    *n_sockets = 0;
    VIR_FREE(bank);
    return NULL;
}

static int virResCtrlGetConfig(int type)
{
    int ret;
    int i;
    char *str;

    /* Read min_cbm_bits from resctrl.
       eg: /sys/fs/resctrl/info/L3/num_closids
    */
    if ((ret = virResCtrlGetInfoStr(type, "num_closids", &str)) < 0) {
        return ret;
    }
    if (virStrToLong_i(str, NULL, 10, &ResCtrlAll[type].num_closid) < 0) {
        return -1;
    }
    VIR_FREE(str);

    /* Read min_cbm_bits from resctrl.
       eg: /sys/fs/resctrl/info/L3/cbm_mask
    */
    if ((ret = virResCtrlGetInfoStr(type, "min_cbm_bits", &str)) < 0) {
        return ret;
    }
    if (virStrToLong_i(str, NULL, 10, &ResCtrlAll[type].min_cbm_bits) < 0) {
        return -1;
    }
    VIR_FREE(str);

    /* Read cbm_mask string from resctrl.
       eg: /sys/fs/resctrl/info/L3/cbm_mask
    */
    if ((ret = virResCtrlGetInfoStr(type, "cbm_mask", &str)) < 0) {
        return ret;
    }

    /* Calculate cbm length from the default cbm_mask. */
    ResCtrlAll[type].cbm_len = strlen(str) * 4;
    VIR_FREE(str);

    /* Get all cache bank informations */
    ResCtrlAll[type].cache_banks = virResCtrlGetCacheBanks(type,
                                                           &(ResCtrlAll[type].num_banks));

    if(ResCtrlAll[type].cache_banks == NULL)
        return -1;

    for( i = 0; i < ResCtrlAll[type].num_banks; i++)
    {
        /*L3CODE and L3DATA shares same L3 resource, so they should
         * have same host_id. */
        if (type == RDT_RESOURCE_L3CODE) {
            ResCtrlAll[type].cache_banks[i].host_id = ResCtrlAll[RDT_RESOURCE_L3DATA].cache_banks[i].host_id;
        }
        else {
            ResCtrlAll[type].cache_banks[i].host_id = host_id++;
        }
    }

    ResCtrlAll[type].enabled = true;

    return ret;
}

int virResCtrlInit(void) {
    int i = 0;
    char *tmp;
    int rc = 0;

    for(i = 0; i < RDT_NUM_RESOURCES; i++) {
        if ((rc = asprintf(&tmp, "%s/%s", RESCTRL_INFO_DIR, ResCtrlAll[i].name)) < 0) {
            continue;
        }
        if (virFileExists(tmp)) {
            if ((rc = virResCtrlGetConfig(i)) < 0 )
                VIR_WARN("Ignor error while get config for %d", i);
        }

        VIR_FREE(tmp);
    }
    return rc;
}

bool virResCtrlAvailable(void) {
    if (!virFileExists(RESCTRL_INFO_DIR))
        return false;
    return true;
}

virResCtrlPtr virResCtrlGet(int type) {
    return &ResCtrlAll[type];
}
