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

VIR_ENUM_IMPL(virResCtrl, RDT_NUM_RESOURCES,
              "l3", "l3data", "l3code", "l2");

#define CONSTRUCT_RESCTRL_PATH(domain_name, item_name) \
do { \
    if (NULL == domain_name) { \
        if (asprintf(&path, "%s/%s", RESCTRL_DIR, item_name) < 0)\
            return -1; \
    } \
    else { \
        if (asprintf(&path, "%s/%s/%s", RESCTRL_DIR, \
                                        domain_name, \
                                        item_name) < 0) \
            return -1;  \
    } \
} while(0)

#define VIR_RESCTRL_ENABLED(type) \
    ResCtrlAll[type].enabled

#define VIR_RESCTRL_GET_SCHEMATA(count) ((1 << count) - 1)

#define VIR_RESCTRL_SET_SCHEMATA(p, type, pos, val) \
    p->schematas[type]->schemata_items[pos] = val

static unsigned int host_id = 0;

/* Global static struct to be maintained which is a interface */
static virResCtrlDomain DomainAll;

/* Global static struct array to be maintained which indicate
 * resource status on a host */
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

/*
 * How many bits is set in schemata
 * eg:
 * virResCtrlBitsNum(1011) = 2 */
static int virResCtrlBitsContinuesNum(unsigned schemata) {
    int ret = 0;
    for (int i = 0; i < MAX_CBM_BIT_LEN; i ++) {
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

    CONSTRUCT_RESCTRL_PATH(domain_name, item_name);

    if (virFileReadAll(path, MAX_FILE_LEN, ret) < 0) {
        rc = -1;
        goto cleanup;
    }

cleanup:
    VIR_FREE(path);
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

    tmp = strstr(buf, ResCtrlAll[type].name);
    end = strchr(tmp, '\n');
    *end = '\0';
    if(VIR_STRDUP(*schemata, tmp) < 0)
        rc = -1;

    VIR_FREE(buf);
    return rc;
}

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

/* Return pointer of and ncount of schemata*/
static virResSchemataPtr virParseSchemata(const char* schemata_str, int* ncount)
{
    const char *p, *q;
    int pos;
    int ischemata;
    virResSchemataPtr schemata;
    virResSchemataItemPtr schemataitems, tmpitem;
    unsigned int socket_no = 0;
    char *tmp;

    if(VIR_ALLOC(schemata) < 0)
        goto cleanup;

    p = q = schemata_str;
    pos = strchr(schemata_str, ':') - p;

    /* calculate cpu socket count */
    *ncount = 1;
    while((q = strchr(p, ';')) != 0) {
        p = q + 1;
        (*ncount)++;
    }

    /* allocat an arrry to store schemata for each socket*/
    if(VIR_ALLOC_N_QUIET(tmpitem, *ncount) < 0)
        goto cleanup;

    schemataitems = tmpitem;

    p = q = schemata_str + pos + 1;

    while(*p != '\0'){
        if (*p == '='){
            q = p + 1;

            tmpitem->socket_no = socket_no++;

            while(*p != ';' && *p != '\0') p++;

            if (VIR_STRNDUP(tmp, q, p-q) < 0)
                goto cleanup;

            if (virStrToLong_i(tmp, NULL, 16, &ischemata) < 0)
                goto cleanup;

            VIR_FREE(tmp);
            tmp = NULL;
            tmpitem->schemata = ischemata;
            tmpitem ++;
            schemata->n_schemata_items +=1;
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
            bank[s_id].cache_left = cache_size - (bank[s_id].cache_min * ResCtrlAll[type].min_cbm_bits);
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

    /* Read num_closids from resctrl.
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

/* Remove the Domain from sysfs, this should only success no pids in tasks
 * of a partition.
 */
static
int virRscctrlRemoveDomain(const char *name)
{
    char *path = NULL;
    int rc = 0;

    if ((rc = asprintf(&path, "%s/%s", RESCTRL_DIR, name)) < 0)
        return rc;
    rc = rmdir(path);
    VIR_FREE(path);
    return rc;
}

/* assemble schemata string*/
static
char* virResCtrlAssembleSchemata(virResSchemataPtr schemata, int type)
{
    virBuffer buf = VIR_BUFFER_INITIALIZER;

    virBufferAsprintf(&buf, "%s:%u=%x", ResCtrlAll[type].name,
                      schemata->schemata_items[0].socket_no,
                      schemata->schemata_items[0].schemata);

    for(int i = 1; i < schemata->n_schemata_items; i++) {
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
    int i;
    int j;
    int k;
    unsigned int tmp_schemata;
    unsigned int default_schemata;
    int pair_type = 0;

    virResDomainPtr header, p;

    header = DomainAll.domains;

    for (i = 0; i < RDT_NUM_RESOURCES; i++) {
        if (VIR_RESCTRL_ENABLED(i)) {

            if (i == RDT_RESOURCE_L3DATA)
                pair_type = RDT_RESOURCE_L3CODE;
            if (i == RDT_RESOURCE_L3CODE)
                pair_type = RDT_RESOURCE_L3DATA;

            for(j = 0; j < header->schematas[i]->n_schemata_items; j ++) {
                p = header->next;
                default_schemata = VIR_RESCTRL_GET_SCHEMATA(ResCtrlAll[i].cbm_len);
                tmp_schemata = 0;
                /* NOTEs: if only header domain, the schemata will be set to default one*/
                for (k = 1; k < DomainAll.num_domains; k++) {
                    tmp_schemata |= p->schematas[i]->schemata_items[j].schemata;
                    if(pair_type > 0)
                        tmp_schemata |= p->schematas[pair_type]->schemata_items[j].schemata;
                    p = p->next;
                }
                /* sys fs doens't let us use 0 */
                int min_bits =  VIR_RESCTRL_GET_SCHEMATA(ResCtrlAll[i].min_cbm_bits);
                if((tmp_schemata & min_bits) == min_bits)
                    tmp_schemata -= min_bits;

                default_schemata ^= tmp_schemata;

                int bitsnum = virResCtrlBitsContinuesNum(default_schemata);
                // calcuate header's schemata
                // NOTES: resctrl sysfs only allow us to set a continues schemata
                header->schematas[i]->schemata_items[j].schemata = VIR_RESCTRL_GET_SCHEMATA(bitsnum);
                ResCtrlAll[i].cache_banks[j].cache_left =
                    (bitsnum - ResCtrlAll[i].min_cbm_bits) * ResCtrlAll[i].cache_banks[j].cache_min;
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
    int i;
    char* tasks;
    unsigned int origin_count = DomainAll.num_domains;
    virResDomainPtr p, pre, del=NULL;
    pre = DomainAll.domains;
    p = pre->next;

    for (i = 1; i < origin_count; i++) {
        if(virResCtrlGetTasks(p->name, &tasks) < 0) {
            VIR_WARN("Failed to get tasks from %s", p->name);
            pre = p;
            p = p->next;
        }
        if(virStringIsEmpty(tasks)) {
            pre->next = p->next;
            if(p->next != NULL)
                p->next->pre = pre;

            del = p;
            p = p->next;
            if(virRscctrlRemoveDomain(del->name) < 0)
                VIR_WARN("Failed to remove partition %s", p->name);

            VIR_DEBUG("Remove partition %s", del->name);

            VIR_FREE(del->name);
            VIR_FREE(del->tasks);
            VIR_FREE(del);
            del = NULL;

            DomainAll.num_domains -=1;
        } else {
            pre = p;
            p = p->next;
        }
        VIR_FREE(tasks);

    }

    return virResCtrlRefreshSchemata();
}

/* Get a domain ptr by domain's name*/
static
virResDomainPtr virResCtrlGetDomain(const char* name) {
    int i;
    virResDomainPtr p = DomainAll.domains;
    for(i = 0; i < DomainAll.num_domains; i++)
    {
        if((p->name) && (strcmp(name, p->name) == 0)) {
            return p;
        }
        p = p->next;
    }
    return NULL;
}

static int
virResCtrlAddTask(virResDomainPtr dom, pid_t pid)
{
    virBuffer buf = VIR_BUFFER_INITIALIZER;
    if(dom->tasks == NULL) {
        virBufferAsprintf(&buf, "%lld\n", (long long)pid);
    } else {
        virBufferAsprintf(&buf, "%s%lld\n", dom->tasks, (long long)pid);
        VIR_FREE(dom->tasks);
    }
    dom->tasks = virBufferContentAndReset(&buf);
    return 0;
}

static int
virResCtrlWrite(const char* name, const char* item, const char* content)
{
    char* path;
    int writefd;
    int rc = -1;

    CONSTRUCT_RESCTRL_PATH(name, item);

    if (!virFileExists(path))
        goto cleanup;

    if ((writefd = open(path, O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR)) < 0)
        goto cleanup;

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
virResDomainPtr virResCtrlLoadDomain(const char* name)
{
    char* schematas;
    char* tasks = NULL;
    virResDomainPtr p;

    if(VIR_ALLOC(p) < 0)
        goto cleanup;

    if(name != NULL && virResCtrlGetTasks(name, &tasks) < 0)
        goto cleanup;

    for(int i = 0; i < RDT_NUM_RESOURCES; i++) {
        if(VIR_RESCTRL_ENABLED(i)) {
            if (virResCtrlGetSchemata(i, name, &schematas) < 0)
                goto cleanup;
            p->schematas[i] = virParseSchemata(schematas, &(p->n_sockets));
            VIR_FREE(schematas);
        }
    }

    p->tasks = tasks;

    if((name != NULL) && (VIR_STRDUP(p->name, name)) < 0)
        goto cleanup;

    return p;

cleanup:
    VIR_FREE(p);
    return NULL;
}

static
virResDomainPtr virResCtrlCreateDomain(const char* name)
{
    char *path;
    mode_t mode = 0755;
    virResDomainPtr p;
    if (asprintf(&path, "%s/%s", RESCTRL_DIR, name) < 0)
	    return NULL;
    if (virDirCreate(path, mode, 0, 0, 0) < 0)
        goto cleanup;

    if((p = virResCtrlLoadDomain(name)) == NULL)
        return p;

    /* sys fs doens't let us use 0.
     * reset schemata to min_bits*/
    for(int i = 0; i < RDT_NUM_RESOURCES; i++) {
        if(VIR_RESCTRL_ENABLED(i)) {
            int min_bits =  VIR_RESCTRL_GET_SCHEMATA(ResCtrlAll[i].min_cbm_bits);
            for(int j = 0; j < p->n_sockets; j++)
                p->schematas[i]->schemata_items[j].schemata = min_bits;
        }
    }

    VIR_FREE(path);
    return p;

cleanup:
    VIR_FREE(path);
    return NULL;
}

static
int virResCtrlDestroyDomain(virResDomainPtr p)
{
    char *path;
    if (asprintf(&path, "%s/%s", RESCTRL_DIR, p->name) < 0)
	    return -1;
    rmdir(path);

    VIR_FREE(p->name);
    p->name = NULL;
    VIR_FREE(p->tasks);
    VIR_FREE(p);
    p = NULL;
    return 0;
}

/* flush domains's information to sysfs*/
static int
virResCtrlFlushDomainToSysfs(virResDomainPtr dom)
{
    int i;
    char* schemata;
    char* tmp;
    int rc = -1;
    virBuffer buf = VIR_BUFFER_INITIALIZER;

    for(i = 0; i < RDT_NUM_RESOURCES; i++) {
        if(VIR_RESCTRL_ENABLED(i)) {
            tmp = virResCtrlAssembleSchemata(dom->schematas[i], i);
            virBufferAsprintf(&buf, "%s\n", tmp);
            VIR_FREE(tmp);
        }
    }

    schemata = virBufferContentAndReset(&buf);

    if(virResCtrlWrite(dom->name, "schemata", schemata) < 0)
        goto cleanup;
    if(!virStringIsEmpty(dom->tasks)
            && virResCtrlWrite(dom->name, "tasks", dom->tasks) < 0)
        goto cleanup;
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

        if(header->next == NULL)
            header->next = tmp;

        if(tmp_pre == NULL)
            tmp->pre = header;
        else {
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
    while(tmp) {
        tmp_pre = tmp;
        tmp = tmp->next;
        VIR_FREE(tmp_pre);
    }
    return NULL;
}

static int
virResCtrlAppendDomain(virResDomainPtr dom)
{
    virResDomainPtr p = DomainAll.domains;
    while(p->next != NULL) p=p->next;
    p->next = dom;
    dom->pre = p;
    DomainAll.num_domains +=1;
    return 0;
}

static int
virResCtrlGetSocketIdByHostID(int type, unsigned int hostid)
{
    int i;
    for( i = 0; i < ResCtrlAll[type].num_banks; i++) {
        if(ResCtrlAll[type].cache_banks[i].host_id == hostid)
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
    int i;
    int count;
    virResDomainPtr p;
    unsigned int tmp_schemata;
    unsigned int schemata_sum = 0;
    int pair_type = 0;

    if(ResCtrlAll[type].cache_banks[sid].cache_left < size) {
        VIR_ERROR("Note enough cache left on bank %u", hostid);
        return -1;
    }
    if ((count = size / ResCtrlAll[type].cache_banks[sid].cache_min) <= 0) {
        VIR_ERROR("Error cache size %llu", size);
        return -1;
    }

    tmp_schemata = VIR_RESCTRL_GET_SCHEMATA(count);

    p = DomainAll.domains;
    p = p->next;

    /* for type is l3code and l3data, we need to deal them specially*/

    if (type == RDT_RESOURCE_L3DATA)
        pair_type =  RDT_RESOURCE_L3CODE;

    if (type == RDT_RESOURCE_L3CODE)
        pair_type = RDT_RESOURCE_L3DATA;

    for (i = 1; i < DomainAll.num_domains; i ++) {
        schemata_sum |= p->schematas[type]->schemata_items[sid].schemata;
        if(pair_type > 0)
            schemata_sum |= p->schematas[pair_type]->schemata_items[sid].schemata;
        p = p->next;
    }

    tmp_schemata = tmp_schemata << (ResCtrlAll[type].cbm_len - count);

    while ((tmp_schemata & schemata_sum) != 0)
        tmp_schemata = tmp_schemata >> 1;
    return tmp_schemata;
}

int virResCtrlSetCacheBanks(virDomainCachetunePtr cachetune,
                            unsigned char* uuid, pid_t pid)
{
    size_t i;
    char name[VIR_UUID_STRING_BUFLEN];
    virResDomainPtr p;
    int type;
    int sid;
    int schemata;

    virUUIDFormat(uuid, name);

    for(i = 0 ; i < cachetune->n_banks; i++) {
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

        for(i = 0 ; i < cachetune->n_banks; i++) {
            if ((type = virResCtrlTypeFromString(
                            cachetune->cache_banks[i].type)) < 0 ) {
                VIR_WARN("Ignore unknown cache type %s.",
                         cachetune->cache_banks[i].type);
                continue;
            }

            if((sid = virResCtrlGetSocketIdByHostID(
                            type, cachetune->cache_banks[i].host_id)) < 0) {
                VIR_WARN("Can not find cache bank host id %u.",
                         cachetune->cache_banks[i].host_id);
                continue;
            }

            if((schemata = virResCtrlCalculateSchemata(
                            type, sid, cachetune->cache_banks[i].host_id,
                            cachetune->cache_banks[i].size)) < 0) {
                VIR_WARN("Failed to set schemata for cache bank id %u",
                         cachetune->cache_banks[i].id);
                continue;
            }

            p->schematas[type]->schemata_items[sid].schemata = schemata;
        }

        virResCtrlAddTask(p, pid);

        if(virResCtrlFlushDomainToSysfs(p) < 0) {
            VIR_WARN("failed to flush domain %s to sysfs", name);
            virResCtrlDestroyDomain(p);
            return -1;
        }
    } else {
        VIR_ERROR("Failed to create a domain in sysfs");
        return -1;
    }

    virResCtrlRefresh();
    /* after refresh, flush header's schemata changes to sys fs */
    if(virResCtrlFlushDomainToSysfs(DomainAll.domains) < 0)
        VIR_WARN("failed to flush domain to sysfs");

    return 0;
}

/* Should be called after pid disappeared, we recalculate
 * schemata of default and flush it to sys fs.
 */
int virResCtrlUpdate(void) {
    int rc;
    if ((rc = virResCtrlRefresh()) < 0)
        VIR_WARN("failed to refresh rescontrol");

    if ((rc = virResCtrlFlushDomainToSysfs(DomainAll.domains)) < 0)
        VIR_WARN("failed to flush domain to sysfs");

    return rc;
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

    DomainAll.domains = virResCtrlGetAllDomains(&(DomainAll.num_domains));

    if((rc = virResCtrlRefresh()) < 0)
        VIR_WARN("failed to refresh resource control");
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
