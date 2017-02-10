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
    char *name;
    virResSchemataPtr schematas[VIR_RDT_RESOURCE_LAST];
    char **tasks;
    size_t n_tasks;
    size_t n_sockets;
    virResDomainPtr pre;
    virResDomainPtr next;
};

/* All resource control domains on this host*/

typedef struct _virResCtrlDomain virResCtrlDomain;
typedef virResCtrlDomain *virResCtrlDomainPtr;
struct _virResCtrlDomain {
    unsigned int num_domains;
    virResDomainPtr domains;

    virMutex lock;
};


static unsigned int host_id;

/* Global static struct to be maintained which is a interface */
static virResCtrlDomain domainall;

/* Global static struct array to be maintained which indicate
 * resource status on a host */
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

/*
 * How many bits is set in schemata
 * eg:
 * virResCtrlBitsNum(1011) = 2 */
static int virResCtrlBitsContinuesNum(unsigned schemata)
{
    size_t i;
    int ret = 0;
    for (i = 0; i < MAX_CBM_BIT_LEN; i ++) {
        if ((schemata & 0x1) == 0x1)
            ret++;
        else
            break;
        schemata = schemata >> 1;
        if (schemata == 0) break;
    }
    return ret;
}

static int virResCtrlGetStr(const char *domain_name, const char *item_name, char **ret)
{
    char *path;
    int rc = 0;
    int readfd;
    int len;

    CONSTRUCT_RESCTRL_PATH(domain_name, item_name);

    if (!virFileExists(path))
        goto cleanup;

    if ((readfd = open(path, O_RDONLY)) < 0)
        goto cleanup;

    /* the lock will be released after readfd closed */
    if (virFileLock(readfd, true, 0, 1, true) < 0) {
        virReportSystemError(errno, _("Unable to lock '%s'"), path);
        goto cleanup;
    }

    len = virFileReadLimFD(readfd, MAX_FILE_LEN, ret);

    if (len < 0) {
        virReportSystemError(errno, _("Failed to read file '%s'"), path);
        goto cleanup;
    }
    rc = 0;

 cleanup:
    VIR_FREE(path);
    VIR_FORCE_CLOSE(readfd);
    return rc;
}

static int virResCtrlGetTasks(const char *domain_name, char **pids)
{
    return virResCtrlGetStr(domain_name, "tasks", pids);
}

static int virResCtrlGetSchemata(const int type, const char *name, char **schemata)
{
    int rc;
    char *tmp, *end;
    char *buf;

    if ((rc = virResCtrlGetStr(name, "schemata", &buf)) < 0)
        return rc;

    tmp = strstr(buf, resctrlall[type].name);
    end = strchr(tmp, '\n');
    *end = '\0';
    if (VIR_STRDUP(*schemata, tmp) < 0)
        rc = -1;

    VIR_FREE(buf);
    return rc;
}

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

/* Return pointer of and ncount of schemata*/
static virResSchemataPtr virParseSchemata(const char *schemata_str, size_t *ncount)
{
    const char *p, *q;
    int pos;
    int ischemata;
    virResSchemataPtr schemata;
    virResSchemataItemPtr schemataitems, tmpitem;
    unsigned int socket_no = 0;
    char *tmp;

    if (VIR_ALLOC(schemata) < 0)
        goto cleanup;

    p = q = schemata_str;
    pos = strchr(schemata_str, ':') - p;

    /* calculate cpu socket count */
    *ncount = 1;
    while ((q = strchr(p, ';')) != 0) {
        p = q + 1;
        (*ncount)++;
    }

    /* allocat an arrry to store schemata for each socket*/
    if (VIR_ALLOC_N_QUIET(tmpitem, *ncount) < 0)
        goto cleanup;

    schemataitems = tmpitem;

    p = q = schemata_str + pos + 1;

    while (*p != '\0') {
        if (*p == '=') {
            q = p + 1;

            tmpitem->socket_no = socket_no++;

            while (*p != ';' && *p != '\0') p++;

            if (VIR_STRNDUP(tmp, q, p-q) < 0)
                goto cleanup;

            if (virStrToLong_i(tmp, NULL, 16, &ischemata) < 0)
                goto cleanup;

            VIR_FREE(tmp);
            tmp = NULL;
            tmpitem->schemata = ischemata;
            tmpitem ++;
            schemata->n_schemata_items += 1;
        }
        p++;
    }

    schemata->schemata_items = schemataitems;
    return schemata;

 cleanup:
    VIR_FREE(schemata);
    VIR_FREE(tmpitem);
    return NULL;
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
            bank[s_id].cache_left = cache_size - (bank[s_id].cache_min * resctrlall[type].min_cbm_bits);
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

    /* Read num_closids from resctrl.
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

/* Remove the Domain from sysfs, this should only success no pids in tasks
 * of a partition.
 */
static
int virResCtrlRemoveDomain(const char *name)
{
    char *path = NULL;
    int rc = 0;

    if ((rc = virAsprintf(&path, "%s/%s", RESCTRL_DIR, name)) < 0)
        return rc;
    rc = rmdir(path);
    VIR_FREE(path);
    return rc;
}

static
int virResCtrlDestroyDomain(virResDomainPtr p)
{
    size_t i;
    int rc;
    if ((rc = virResCtrlRemoveDomain(p->name)) < 0)
        VIR_WARN("Failed to removed partition %s", p->name);

    VIR_FREE(p->name);
    p->name = NULL;
    for (i = 0; i < p->n_tasks; i ++)
        VIR_FREE(p->tasks[i]);
    VIR_FREE(p);
    p = NULL;
    return rc;
}


/* assemble schemata string*/
static
char* virResCtrlAssembleSchemata(virResSchemataPtr schemata, int type)
{
    virBuffer buf = VIR_BUFFER_INITIALIZER;
    size_t i;

    virBufferAsprintf(&buf, "%s:%u=%x", resctrlall[type].name,
                      schemata->schemata_items[0].socket_no,
                      schemata->schemata_items[0].schemata);

    for (i = 1; i < schemata->n_schemata_items; i++) {
        virBufferAsprintf(&buf, ";%u=%x",
                       schemata->schemata_items[i].socket_no,
                       schemata->schemata_items[i].schemata);
    }

    return virBufferContentAndReset(&buf);
}

/* Refresh default domains' schemata
 */
static
int virResCtrlRefreshSchemata(void)
{
    size_t i, j, k;
    unsigned int tmp_schemata;
    unsigned int default_schemata;
    int pair_type = 0;

    virResDomainPtr header, p;

    header = domainall.domains;

    if (!header)
        return 0;

    for (i = 0; i < VIR_RDT_RESOURCE_LAST; i++) {
        if (VIR_RESCTRL_ENABLED(i)) {

            if (i == VIR_RDT_RESOURCE_L3DATA)
                pair_type = VIR_RDT_RESOURCE_L3CODE;
            if (i == VIR_RDT_RESOURCE_L3CODE)
                pair_type = VIR_RDT_RESOURCE_L3DATA;

            for (j = 0; j < header->schematas[i]->n_schemata_items; j ++) {
                p = header->next;
                default_schemata = VIR_RESCTRL_GET_SCHEMATA(resctrlall[i].cbm_len);
                tmp_schemata = 0;
                /* NOTEs: if only header domain, the schemata will be set to default one*/
                for (k = 1; k < domainall.num_domains; k++) {
                    tmp_schemata |= p->schematas[i]->schemata_items[j].schemata;
                    if (pair_type > 0)
                        tmp_schemata |= p->schematas[pair_type]->schemata_items[j].schemata;
                    p = p->next;
                }
                /* sys fs doens't let us use 0 */
                int min_bits = VIR_RESCTRL_GET_SCHEMATA(resctrlall[i].min_cbm_bits);
                if ((tmp_schemata & min_bits) == min_bits)
                    tmp_schemata -= min_bits;

                default_schemata ^= tmp_schemata;

                int bitsnum = virResCtrlBitsContinuesNum(default_schemata);
                // calcuate header's schemata
                // NOTES: resctrl sysfs only allow us to set a continues schemata
                header->schematas[i]->schemata_items[j].schemata = VIR_RESCTRL_GET_SCHEMATA(bitsnum);
                resctrlall[i].cache_banks[j].cache_left =
                    (bitsnum - resctrlall[i].min_cbm_bits) * resctrlall[i].cache_banks[j].cache_min;
            }
        }
    }

    return 0;

}

/* Refresh all domains', remove the domains which has no task ids.
 * This will be used after VM pause, restart, destroy etc.
 */
static int
virResCtrlRefresh(void)
{
    size_t i;
    char* tasks;
    unsigned int origin_count = domainall.num_domains;
    virResDomainPtr p, pre, del;
    pre = domainall.domains;

    virMutexLock(&domainall.lock);

    p = del = NULL;
    if (pre)
        p = pre->next;

    for (i = 1; i < origin_count && p; i++) {
        if (virResCtrlGetTasks(p->name, &tasks) < 0) {
            VIR_WARN("Failed to get tasks from %s", p->name);
            pre = p;
            p = p->next;
        }
        if (virStringIsEmpty(tasks)) {
            pre->next = p->next;
            if (p->next != NULL)
                p->next->pre = pre;

            del = p;
            p = p->next;
            virResCtrlDestroyDomain(del);
            domainall.num_domains -= 1;
        } else {
            pre = p;
            p = p->next;
        }
        VIR_FREE(tasks);
    }

    virResCtrlRefreshSchemata();
    virMutexUnlock(&domainall.lock);
    return 0;
}

/* Get a domain ptr by domain's name*/
static
virResDomainPtr virResCtrlGetDomain(const char* name) {
    size_t i;
    virResDomainPtr p = domainall.domains;
    for (i = 0; i < domainall.num_domains; i++) {
        if ((p->name) && STREQ(name, p->name))
            return p;
        p = p->next;
    }
    return NULL;
}

static int
virResCtrlAddTask(virResDomainPtr dom, pid_t pid)
{
    size_t maxtasks;

    if (VIR_RESIZE_N(dom->tasks, maxtasks, dom->n_tasks, 1) < 0)
        return -1;

    if (virAsprintf(&(dom->tasks[dom->n_tasks]), "%llu", (long long)pid) < 0)
        return -1;

    dom->n_tasks += 1;
    return 0;
}

static int
virResCtrlWrite(const char *name, const char *item, const char *content)
{
    char *path;
    int writefd;
    int rc = -1;

    CONSTRUCT_RESCTRL_PATH(name, item);

    if (!virFileExists(path))
        goto cleanup;

    if ((writefd = open(path, O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR)) < 0)
        goto cleanup;

    /* the lock will be released after writefd closed */
    if (virFileLock(writefd, false, 0, 0, true) < 0) {
        virReportSystemError(errno, _("Unable to lock '%s'"), path);
        goto cleanup;
    }

    if (safewrite(writefd, content, strlen(content)) < 0)
        goto cleanup;

    rc = 0;

 cleanup:
    VIR_FREE(path);
    VIR_FORCE_CLOSE(writefd);
    return rc;
}

/* if name == NULL we load default schemata */
static
virResDomainPtr virResCtrlLoadDomain(const char *name)
{
    char *schematas;
    virResDomainPtr p;
    size_t i;

    if (VIR_ALLOC(p) < 0)
        goto cleanup;

    for (i = 0; i < VIR_RDT_RESOURCE_LAST; i++) {
        if (VIR_RESCTRL_ENABLED(i)) {
            if (virResCtrlGetSchemata(i, name, &schematas) < 0)
                goto cleanup;
            p->schematas[i] = virParseSchemata(schematas, &(p->n_sockets));
            VIR_FREE(schematas);
        }
    }

    p->tasks = NULL;
    p->n_tasks = 0;

    if ((name != NULL) && (VIR_STRDUP(p->name, name)) < 0)
        goto cleanup;

    return p;

 cleanup:
    VIR_FREE(p);
    return NULL;
}

static
virResDomainPtr virResCtrlCreateDomain(const char *name)
{
    char *path;
    mode_t mode = 0755;
    virResDomainPtr p;
    size_t i, j;
    if (virAsprintf(&path, "%s/%s", RESCTRL_DIR, name) < 0)
        return NULL;

    if (virDirCreate(path, mode, 0, 0, 0) < 0)
        goto cleanup;

    if ((p = virResCtrlLoadDomain(name)) == NULL)
        return p;

    /* sys fs doens't let us use 0.
     * reset schemata to min_bits*/
    for (i = 0; i < VIR_RDT_RESOURCE_LAST; i++) {
        if (VIR_RESCTRL_ENABLED(i)) {
            int min_bits =  VIR_RESCTRL_GET_SCHEMATA(resctrlall[i].min_cbm_bits);
            for (j = 0; j < p->n_sockets; j++)
                p->schematas[i]->schemata_items[j].schemata = min_bits;
        }
    }

    VIR_FREE(path);
    return p;

 cleanup:
    VIR_FREE(path);
    return NULL;
}

/* flush domains's information to sysfs*/
static int
virResCtrlFlushDomainToSysfs(virResDomainPtr dom)
{
    size_t i;
    char* schemata;
    char* tmp;
    int rc = -1;
    virBuffer buf = VIR_BUFFER_INITIALIZER;

    for (i = 0; i < VIR_RDT_RESOURCE_LAST; i++) {
        if (VIR_RESCTRL_ENABLED(i)) {
            tmp = virResCtrlAssembleSchemata(dom->schematas[i], i);
            virBufferAsprintf(&buf, "%s\n", tmp);
            VIR_FREE(tmp);
        }
    }

    schemata = virBufferContentAndReset(&buf);

    if (virResCtrlWrite(dom->name, "schemata", schemata) < 0)
        goto cleanup;

    if (dom->n_tasks > 0) {
        for (i = 0; i < dom->n_tasks; i++) {
        if (virResCtrlWrite(dom->name, "tasks", dom->tasks[i]) < 0)
            goto cleanup;
        }
    }

    rc = 0;

 cleanup:
    VIR_FREE(schemata);
    return rc;
}

static virResDomainPtr virResCtrlGetAllDomains(unsigned int *len)
{
    struct dirent *ent;
    DIR *dp = NULL;
    int direrr;

    *len = 0;
    virResDomainPtr header, tmp, tmp_pre;
    header = tmp = tmp_pre = NULL;
    if (virDirOpenQuiet(&dp, RESCTRL_DIR) < 0) {
        if (errno == ENOENT)
            return NULL;
        VIR_ERROR(_("Unable to open %s (%d)"), RESCTRL_DIR, errno);
        goto cleanup;
    }

    header = virResCtrlLoadDomain(NULL);
    if (header == NULL)
        goto cleanup;

    header->next = NULL;

    *len = 1;

    while ((direrr = virDirRead(dp, &ent, NULL)) > 0) {
        if ((ent->d_type != DT_DIR) || STREQ(ent->d_name, "info"))
            continue;

        tmp = virResCtrlLoadDomain(ent->d_name);
        if (tmp == NULL)
            goto cleanup;

        tmp->next = NULL;

        if (header->next == NULL)
            header->next = tmp;

        if (tmp_pre == NULL) {
            tmp->pre = header;
        } else {
            tmp->pre = tmp_pre;
            tmp_pre->next = tmp;
        }

        tmp_pre = tmp;
        (*len) ++;
    }
    return header;

 cleanup:
    VIR_DIR_CLOSE(dp);
    tmp_pre = tmp = header;
    while (tmp) {
        tmp_pre = tmp;
        tmp = tmp->next;
        VIR_FREE(tmp_pre);
    }
    return NULL;
}

static int
virResCtrlAppendDomain(virResDomainPtr dom)
{
    virResDomainPtr p = domainall.domains;

    virMutexLock(&domainall.lock);

    while (p->next != NULL) p = p->next;
    p->next = dom;
    dom->pre = p;
    domainall.num_domains += 1;

    virMutexUnlock(&domainall.lock);
    return 0;
}

static int
virResCtrlGetSocketIdByHostID(int type, unsigned int hostid)
{
    size_t i;
    for (i = 0; i < resctrlall[type].num_banks; i++) {
        if (resctrlall[type].cache_banks[i].host_id == hostid)
            return i;
    }
    return -1;
}

static int
virResCtrlCalculateSchemata(int type,
                            int sid,
                            unsigned hostid,
                            unsigned long long size)
{
    size_t i;
    int count;
    int rc = -1;
    virResDomainPtr p;
    unsigned int tmp_schemata;
    unsigned int schemata_sum = 0;
    int pair_type = 0;

    virMutexLock(&resctrlall[type].cache_banks[sid].lock);

    if (resctrlall[type].cache_banks[sid].cache_left < size) {
        VIR_ERROR(_("Not enough cache left on bank %u"), hostid);
        goto cleanup;
    }

    if ((count = size / resctrlall[type].cache_banks[sid].cache_min) <= 0) {
        VIR_ERROR(_("Error cache size %llu"), size);
        goto cleanup;
    }

    tmp_schemata = VIR_RESCTRL_GET_SCHEMATA(count);

    p = domainall.domains;
    p = p->next;

    /* for type is l3code and l3data, we need to deal them specially*/
    if (type == VIR_RDT_RESOURCE_L3DATA)
        pair_type = VIR_RDT_RESOURCE_L3CODE;

    if (type == VIR_RDT_RESOURCE_L3CODE)
        pair_type = VIR_RDT_RESOURCE_L3DATA;

    for (i = 1; i < domainall.num_domains; i++) {
        schemata_sum |= p->schematas[type]->schemata_items[sid].schemata;
        if (pair_type > 0)
            schemata_sum |= p->schematas[pair_type]->schemata_items[sid].schemata;
        p = p->next;
    }

    tmp_schemata = tmp_schemata << (resctrlall[type].cbm_len - count);

    while ((tmp_schemata & schemata_sum) != 0)
        tmp_schemata = tmp_schemata >> 1;

    resctrlall[type].cache_banks[sid].cache_left -= size;
    if (pair_type > 0)
        resctrlall[pair_type].cache_banks[sid].cache_left = resctrlall[type].cache_banks[sid].cache_left;

    rc = tmp_schemata;

 cleanup:
    virMutexUnlock(&resctrlall[type].cache_banks[sid].lock);
    return rc;
}

int virResCtrlSetCacheBanks(virDomainCachetunePtr cachetune,
                            unsigned char *uuid, pid_t *pids, int npid)
{
    size_t i;
    char name[VIR_UUID_STRING_BUFLEN];
    virResDomainPtr p;
    int type;
    int pair_type = -1;
    int sid;
    int schemata;

    virUUIDFormat(uuid, name);

    for (i = 0; i < cachetune->n_banks; i++) {
        VIR_DEBUG("cache_banks %u, %u, %llu, %s",
                 cachetune->cache_banks[i].id,
                 cachetune->cache_banks[i].host_id,
                 cachetune->cache_banks[i].size,
                 cachetune->cache_banks[i].type);
    }

    if (cachetune->n_banks < 1)
        return 0;

    p = virResCtrlGetDomain(name);
    if (p == NULL) {
        VIR_DEBUG("no domain name %s found, create new one!", name);
        p = virResCtrlCreateDomain(name);
    }

    if (p != NULL) {

        virResCtrlAppendDomain(p);

        for (i = 0; i < cachetune->n_banks; i++) {
            if ((type = virResCtrlTypeFromString(
                            cachetune->cache_banks[i].type)) < 0) {
                VIR_WARN("Ignore unknown cache type %s.",
                         cachetune->cache_banks[i].type);
                continue;
            }
            /* use cdp compatible mode */
            if (!VIR_RESCTRL_ENABLED(type) &&
                    (type == VIR_RDT_RESOURCE_L3) &&
                    VIR_RESCTRL_ENABLED(VIR_RDT_RESOURCE_L3DATA)) {
                type = VIR_RDT_RESOURCE_L3DATA;
                pair_type = VIR_RDT_RESOURCE_L3CODE;
            }

            if ((sid = virResCtrlGetSocketIdByHostID(
                            type, cachetune->cache_banks[i].host_id)) < 0) {
                VIR_WARN("Can not find cache bank host id %u.",
                         cachetune->cache_banks[i].host_id);
                continue;
            }

            if ((schemata = virResCtrlCalculateSchemata(
                            type, sid, cachetune->cache_banks[i].host_id,
                            cachetune->cache_banks[i].size)) < 0) {
                VIR_WARN("Failed to set schemata for cache bank id %u",
                         cachetune->cache_banks[i].id);
                continue;
            }

            p->schematas[type]->schemata_items[sid].schemata = schemata;
            if (pair_type > 0)
                p->schematas[pair_type]->schemata_items[sid].schemata = schemata;
        }

        for (i = 0; i < npid; i++)
            virResCtrlAddTask(p, pids[i]);

        if  (virResCtrlFlushDomainToSysfs(p) < 0) {
            VIR_WARN("failed to flush domain %s to sysfs", name);
            virResCtrlDestroyDomain(p);
            return -1;
        }
    } else {
        VIR_ERROR(_("Failed to create a domain in sysfs"));
        return -1;
    }

    virResCtrlRefresh();
    /* after refresh, flush header's schemata changes to sys fs */
    if (virResCtrlFlushDomainToSysfs(domainall.domains) < 0)
        VIR_WARN("failed to flush domain to sysfs");

    return 0;
}

/* Should be called after pid disappeared, we recalculate
 * schemata of default and flush it to sys fs.
 */
int virResCtrlUpdate(void)
{
    int rc;

    if ((rc = virResCtrlRefresh()) < 0)
        VIR_WARN("failed to refresh rescontrol");

    if ((rc = virResCtrlFlushDomainToSysfs(domainall.domains)) < 0)
        VIR_WARN("failed to flush domain to sysfs");

    return rc;
}

int
virResCtrlInit(void)
{
    size_t i, j;
    char *tmp;
    int rc = 0;

    for (i = 0; i < VIR_RDT_RESOURCE_LAST; i++) {
        if ((rc = virAsprintf(&tmp, "%s/%s", RESCTRL_INFO_DIR, resctrlall[i].name)) < 0) {
            VIR_ERROR(_("Failed to initialize resource control config"));
            return -1;
        }
        if (virFileExists(tmp)) {
            if ((rc = virResCtrlGetConfig(i)) < 0) {
                VIR_ERROR(_("Failed to get resource control config"));
                return -1;
            }
        }
        VIR_FREE(tmp);
    }

    domainall.domains = virResCtrlGetAllDomains(&(domainall.num_domains));

    for (i = 0; i < VIR_RDT_RESOURCE_LAST; i++) {
        if (VIR_RESCTRL_ENABLED(i)) {
            for (j = 0; j < resctrlall[i].num_banks; j++) {
                if (virMutexInit(&resctrlall[i].cache_banks[j].lock) < 0) {
                    virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                            _("Unable to initialize mutex"));
                    return -1;
                }
            }
        }
    }

    if (virMutexInit(&domainall.lock) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                _("Unable to initialize mutex"));
        return -1;
    }

    if ((rc = virResCtrlRefresh()) < 0)
        VIR_ERROR(_("Failed to refresh resource control"));
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
