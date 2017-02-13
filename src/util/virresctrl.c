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
#define MAX_CPU_SOCKET_NUM 8
#define MAX_CBM_BIT_LEN 32
#define MAX_SCHEMATA_LEN 1024
#define MAX_FILE_LEN ( 10 * 1024 * 1024)
#define RESCTRL_DIR "/sys/fs/resctrl"
#define RESCTRL_INFO_DIR "/sys/fs/resctrl/info"
#define SYSFS_SYSTEM_PATH "/sys/devices/system"

VIR_ENUM_IMPL(virResCtrl, VIR_RDT_RESOURCE_LAST,
              "l3", "l3data", "l3code", "l2");

#define CONSTRUCT_RESCTRL_PATH(domain_name, item_name) \
do { \
    if (NULL == domain_name) { \
        if (virAsprintf(&path, "%s/%s", RESCTRL_DIR, item_name) < 0)\
            return -1; \
    } else { \
        if (virAsprintf(&path, "%s/%s/%s", RESCTRL_DIR, \
                                        domain_name, \
                                        item_name) < 0) \
            return -1;  \
    } \
} while (0)

#define VIR_RESCTRL_ENABLED(type) \
    resctrlall[type].enabled

#define VIR_RESCTRL_GET_SCHEMATA(count) ((1 << count) - 1)

#define VIR_RESCTRL_SET_SCHEMATA(p, type, pos, val) \
    p->schematas[type]->schemata_items[pos] = val


static unsigned int host_id;

static virResCtrl resctrlall[] = {
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

    if (virAsprintf(&path, "%s/%s/%s", RESCTRL_INFO_DIR, resctrlall[type].name, item) < 0)
        return -1;
    if (virFileReadAll(path, 10, str) < 0) {
        ret = -1;
        goto cleanup;
    }

    if ((tmp = strchr(*str, '\n'))) *tmp = '\0';

 cleanup:
    VIR_FREE(path);
    return ret;
}


static int virResCtrlGetCPUValue(const char *path, char **value)
{
    int ret = -1;
    char *tmp;

    if (virFileReadAll(path, 10, value) < 0)  goto cleanup;
    if ((tmp = strchr(*value, '\n'))) *tmp = '\0';
    ret = 0;

 cleanup:
    return ret;
}

static int virResctrlGetCPUSocketID(const size_t cpu, int *socket_id)
{
    int ret = -1;
    char *physical_package_path = NULL;
    char *physical_package = NULL;
    if (virAsprintf(&physical_package_path,
                    "%s/cpu/cpu%zu/topology/physical_package_id",
                    SYSFS_SYSTEM_PATH, cpu) < 0) {
        return -1;
    }

    if (virResCtrlGetCPUValue(physical_package_path,
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
    char *cache_dir = NULL;
    char *cache_str = NULL;
    char *tmp;
    int carry = -1;

    if (virAsprintf(&cache_dir,
                    "%s/cpu/cpu%zu/cache/index%d/size",
                    SYSFS_SYSTEM_PATH, cpu, type) < 0)
        return -1;

    if (virResCtrlGetCPUValue(cache_dir, &cache_str) < 0)
        goto cleanup;

    tmp = cache_str;

    while (*tmp != '\0') tmp++;

    if (*(tmp - 1) == 'K') {
        *(tmp - 1) = '\0';
        carry = 1;
    } else if (*(tmp - 1) == 'M') {
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
static virResCacheBankPtr virResCtrlGetCacheBanks(int type, int *n_sockets)
{
    int npresent_cpus;
    int idx = -1;
    size_t i;
    virResCacheBankPtr bank;

    *n_sockets = 1;
    if ((npresent_cpus = virHostCPUGetCount()) < 0)
        return NULL;

    if (type == VIR_RDT_RESOURCE_L3
            || type == VIR_RDT_RESOURCE_L3DATA
            || type == VIR_RDT_RESOURCE_L3CODE)
        idx = 3;
    else if (type == VIR_RDT_RESOURCE_L2)
        idx = 2;

    if (idx == -1)
        return NULL;

    if (VIR_ALLOC_N(bank, *n_sockets) < 0) {
        *n_sockets = 0;
        return NULL;
    }

    for (i = 0; i < npresent_cpus; i ++) {
        int s_id;
        int cache_size;

        if (virResctrlGetCPUSocketID(i, &s_id) < 0)
            goto error;

        if (s_id > (*n_sockets - 1)) {
            size_t cur = *n_sockets;
            size_t exp = s_id - (*n_sockets) + 1;
            if (VIR_EXPAND_N(bank, cur, exp) < 0)
                goto error;
            *n_sockets = s_id + 1;
        }

        if (bank[s_id].cpu_mask == NULL) {
            if (!(bank[s_id].cpu_mask = virBitmapNew(npresent_cpus)))
                goto error;
        }

        ignore_value(virBitmapSetBit(bank[s_id].cpu_mask, i));

        if (bank[s_id].cache_size == 0) {
           if (virResCtrlGetCPUCache(i, idx, &cache_size) < 0)
                goto error;

            bank[s_id].cache_size = cache_size;
            bank[s_id].cache_min = cache_size / resctrlall[type].cbm_len;
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
    size_t i;
    char *str;

    /* Read min_cbm_bits from resctrl.
       eg: /sys/fs/resctrl/info/L3/num_closids
    */
    if ((ret = virResCtrlGetInfoStr(type, "num_closids", &str)) < 0)
        return ret;

    if (virStrToLong_i(str, NULL, 10, &resctrlall[type].num_closid) < 0)
        return -1;

    VIR_FREE(str);

    /* Read min_cbm_bits from resctrl.
       eg: /sys/fs/resctrl/info/L3/cbm_mask
    */
    if ((ret = virResCtrlGetInfoStr(type, "min_cbm_bits", &str)) < 0)
        return ret;

    if (virStrToLong_i(str, NULL, 10, &resctrlall[type].min_cbm_bits) < 0)
        return -1;

    VIR_FREE(str);

    /* Read cbm_mask string from resctrl.
       eg: /sys/fs/resctrl/info/L3/cbm_mask
    */
    if ((ret = virResCtrlGetInfoStr(type, "cbm_mask", &str)) < 0)
        return ret;

    /* Calculate cbm length from the default cbm_mask. */
    resctrlall[type].cbm_len = strlen(str) * 4;
    VIR_FREE(str);

    /* Get all cache bank informations */
    resctrlall[type].cache_banks = virResCtrlGetCacheBanks(type,
                                                           &(resctrlall[type].num_banks));

    if (resctrlall[type].cache_banks == NULL)
        return -1;

    for (i = 0; i < resctrlall[type].num_banks; i++) {
        /*L3CODE and L3DATA shares same L3 resource, so they should
         * have same host_id. */
        if (type == VIR_RDT_RESOURCE_L3CODE)
            resctrlall[type].cache_banks[i].host_id = resctrlall[VIR_RDT_RESOURCE_L3DATA].cache_banks[i].host_id;
        else
            resctrlall[type].cache_banks[i].host_id = host_id++;
    }

    resctrlall[type].enabled = true;

    return ret;
}

int
virResCtrlInit(void)
{
    size_t i = 0;
    char *tmp;
    int rc = 0;

    for (i = 0; i < VIR_RDT_RESOURCE_LAST; i++) {
        if ((rc = virAsprintf(&tmp, "%s/%s", RESCTRL_INFO_DIR, resctrlall[i].name)) < 0) {
            VIR_ERROR(_("Failed to initialize resource control config"));
            return -1;
        }
        if (virFileExists(tmp)) {
            if ((rc = virResCtrlGetConfig(i)) < 0)
                VIR_ERROR(_("Failed to get resource control config"));
                return -1;
        }

        VIR_FREE(tmp);
    }
    return rc;
}

/*
 * Test whether the host support resource control
 */
bool
virResCtrlAvailable(void)
{
    if (!virFileExists(RESCTRL_INFO_DIR))
        return false;
    return true;
}

/*
 * Return an virResCtrlPtr point to virResCtrl object,
 * We should not modify it out side of virresctrl.c
 */
virResCtrlPtr
virResCtrlGet(int type)
{
    return &resctrlall[type];
}
