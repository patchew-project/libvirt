/*
 * virresctrl.c: methods for managing resource control
 *
 * Copyright (C) 2017 Intel, Inc.
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
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "virresctrl.h"
#include "virerror.h"
#include "virlog.h"
#include "viralloc.h"
#include "virstring.h"
#include "virfile.h"

VIR_LOG_INIT("util.resctrl");

#define VIR_FROM_THIS VIR_FROM_RESCTRL
#define SYSFS_RESCTRL_PATH "/sys/fs/resctrl"
#define MAX_CBM_LEN 20

VIR_ENUM_IMPL(virResctrl, VIR_RESCTRL_TYPE_LAST,
              "L3",
              "L3CODE",
              "L3DATA")

/**
 * a virResctrlGroup represents a resource control group, it's a directory
 * under /sys/fs/resctrl.
 * e.g. /sys/fs/resctrl/CG1
 * |-- cpus
 * |-- schemata
 * `-- tasks
 * # cat schemata
 * L3DATA:0=fffff;1=fffff
 * L3CODE:0=fffff;1=fffff
 *
 * Besides, it can also represent the default resource control group of the
 * host.
 */

typedef struct _virResctrlGroup virResctrlGroup;
typedef virResctrlGroup *virResctrlGroupPtr;
struct _virResctrlGroup {
    char *name; /* resource group name, NULL for default host group */
    size_t n_tasks; /* number of tasks assigned to the resource group */
    char **tasks; /* task id list */

    virResctrlSchemataPtr schemata[VIR_RESCTRL_TYPE_LAST]; /* Array for schemata */
};

/* All resource control groups on this host, including default resource group */
typedef struct _virResctrlHost virResctrlHost;
typedef virResctrlHost *virResctrlHostPtr;
struct _virResctrlHost {
    size_t n_groups; /* number of resource control group */
    virResctrlGroupPtr *groups; /* list of resource control group */
};

void
virResctrlFreeSchemata(virResctrlSchemataPtr ptr)
{
    size_t i;

    if (!ptr)
        return;
    for (i = 0; i < ptr->n_masks; i++) {
        virBitmapFree(ptr->masks[i]->mask);
        VIR_FREE(ptr->masks[i]);
    }

    VIR_FREE(ptr->masks);
    VIR_FREE(ptr);
    ptr = NULL;
}

static void
virResctrlFreeGroup(virResctrlGroupPtr ptr)
{
    size_t i;

    if (!ptr)
        return;


    virStringListFree(ptr->tasks);
    VIR_FREE(ptr->name);

    for (i = 0; i < VIR_RESCTRL_TYPE_LAST; i++)
        virResctrlFreeSchemata(ptr->schemata[i]);

    VIR_FREE(ptr);
    ptr = NULL;
}

/* Return specify type of schemata string from schematalval.
   e.g., 0=f;1=f */
static int
virResctrlGetSchemataString(virResctrlType type,
                            const char *schemataval,
                            char **schematastr)
{
    int rc = -1;
    char *prefix = NULL;
    char **lines = NULL;

    if (virAsprintf(&prefix,
                    "%s:",
                    virResctrlTypeToString(type)) < 0)
        return -1;

    lines = virStringSplit(schemataval, "\n", 0);

    if (VIR_STRDUP(*schematastr,
                   virStringListGetFirstWithPrefix(lines, prefix)) < 0)
        goto cleanup;

    if (*schematastr == NULL)
        rc = -1;
    else
        rc = 0;

 cleanup:
    VIR_FREE(prefix);
    virStringListFree(lines);
    return rc;
}

static
virBitmapPtr virResctrlMask2Bitmap(const char *mask)
{
    virBitmapPtr bitmap;
    unsigned int tmp;
    size_t i;

    if (virStrToLong_ui(mask, NULL, 16, &tmp) < 0)
        return NULL;

    bitmap = virBitmapNewQuiet(MAX_CBM_LEN);

    for (i = 0; i < MAX_CBM_LEN; i++) {
        if (((tmp & 0x1) == 0x1) &&
                (virBitmapSetBit(bitmap, i) < 0))
            goto error;
        tmp = tmp >> 1;
    }

    return bitmap;

 error:
    virBitmapFree(bitmap);
    return NULL;
}

char *virResctrlBitmap2String(virBitmapPtr bitmap)
{
    char *tmp;
    char *ret = NULL;
    char *p;
    tmp = virBitmapString(bitmap);
    // skip "0x"
    p = tmp + 2;

    // find first non-0 position
    while (*++p == '0');

    if (VIR_STRDUP(ret, p) < 0)
        ret = NULL;

    VIR_FREE(tmp);
    return ret;
}

static int
virResctrlParseSchemata(const char* schemata_str,
                        virResctrlSchemataPtr schemata)
{
    VIR_DEBUG("schemata_str=%s, schemata=%p", schemata_str, schemata);

    int ret = -1;
    size_t i;
    virResctrlMaskPtr mask;
    char **schemata_list;
    char *mask_str;

    /* parse 0=fffff;1=f */
    schemata_list = virStringSplit(schemata_str, ";", 0);

    if (!schemata_list)
        goto cleanup;

    for (i = 0; schemata_list[i] != NULL; i++) {
        /* parse 0=fffff */
        mask_str = strchr(schemata_list[i], '=');

        if (!mask_str)
            goto cleanup;

        if (VIR_ALLOC(mask) < 0)
            goto cleanup;

        mask->cache_id = i;
        mask->mask = virResctrlMask2Bitmap(mask_str + 1);

        if (VIR_APPEND_ELEMENT(schemata->masks,
                               schemata->n_masks,
                               mask) < 0) {
            VIR_FREE(mask);
            goto cleanup;
        }

    }
    ret = 0;

 cleanup:
    virStringListFree(schemata_list);
    return ret;
}

static int
virResctrlLoadGroup(const char *name,
                    virResctrlHostPtr host)
{
    VIR_DEBUG("name=%s, host=%p\n", name, host);

    int ret = -1;
    char *schemataval = NULL;
    char *schemata_str = NULL;
    virResctrlType i;
    int rv;
    virResctrlGroupPtr grp;
    virResctrlSchemataPtr schemata;

    rv = virFileReadValueString(&schemataval,
                                SYSFS_RESCTRL_PATH "/%s/schemata",
                                name ? name : "");

    if (rv < 0)
        return -1;

    if (VIR_ALLOC(grp) < 0)
        goto cleanup;

    if (VIR_STRDUP(grp->name, name) < 0)
        goto cleanup;

    for (i = 0; i < VIR_RESCTRL_TYPE_LAST; i++) {
        rv = virResctrlGetSchemataString(i, schemataval, &schemata_str);

        if (rv < 0)
            continue;

        if (VIR_ALLOC(schemata) < 0)
            goto cleanup;

        schemata->type = i;

        if (virResctrlParseSchemata(schemata_str, schemata) < 0) {
            VIR_FREE(schemata);
            VIR_FREE(schemata_str);
            goto cleanup;
        }

        grp->schemata[i] = schemata;
        VIR_FREE(schemata_str);
    }

    if (VIR_APPEND_ELEMENT(host->groups,
                           host->n_groups,
                           grp) < 0) {
        virResctrlFreeGroup(grp);
        goto cleanup;
    }

    ret = 0;

 cleanup:
    VIR_FREE(schemataval);
    return ret;
}

static int
virResctrlLoadHost(virResctrlHostPtr host)
{
    int ret = -1;
    int rv = -1;
    DIR *dirp = NULL;
    char *path = NULL;
    struct dirent *ent;

    VIR_DEBUG("host=%p\n", host);

    rv = virDirOpenIfExists(&dirp, SYSFS_RESCTRL_PATH);

    if (rv < 0)
        goto cleanup;

    /* load default group first */
    if (virResctrlLoadGroup(NULL, host) < 0)
        goto cleanup;

    while ((rv = virDirRead(dirp, &ent, path)) > 0) {
        /* resctrl is not hierarchical, only read directory under
           /sys/fs/resctrl */
        if ((ent->d_type != DT_DIR) || STREQ(ent->d_name, "info"))
            continue;

        if (virResctrlLoadGroup(ent->d_name, host) < 0)
            goto cleanup;
    }

    ret = 0;

 cleanup:
    virDirClose(&dirp);
    return ret;
}

static void
virResctrlRefreshHost(virResctrlHostPtr host)
{
    virResctrlGroupPtr default_grp = NULL;
    virResctrlGroupPtr grp = NULL;
    virResctrlSchemataPtr schemata = NULL;
    size_t i, j;
    virResctrlType t;

    default_grp = host->groups[0];

    /* Loop each other resource group except default group */
    for (i = 1; i < host->n_groups; i++) {
        grp = host->groups[i];
        for (t = 0; t < VIR_RESCTRL_TYPE_LAST; t++) {
            schemata = grp->schemata[t];
            if (schemata != NULL) {
                for (j = 0; j < schemata->n_masks; j++)
                    virBitmapSubtract(default_grp->schemata[t]->masks[j]->mask,
                                      schemata->masks[j]->mask);
            }
        }
    }
}

virResctrlSchemataPtr
virResctrlGetFreeCache(virResctrlType type)
{
    VIR_DEBUG("type=%d", type);
    virResctrlHostPtr host = NULL;
    virResctrlSchemataPtr schemata = NULL;
    size_t i;

    if (VIR_ALLOC(host) < 0)
        return NULL;

    if (virResctrlLoadHost(host) < 0)
        return NULL;

    /* default group come the first one */
    virResctrlRefreshHost(host);

    schemata = host->groups[0]->schemata[type];

    for (i = 1; i < host->n_groups; i++)
        virResctrlFreeGroup(host->groups[i]);
    VIR_FREE(host->groups);
    host = NULL;

    return schemata;
}
