/*
 * virmacmap.c: MAC address <-> Domain name mapping
 *
 * Copyright (C) 2016 Red Hat, Inc.
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
 *     Michal Privoznik <mprivozn@redhat.com>
 */

#include <config.h>

#include "virmacmap.h"
#include "virobject.h"
#include "virlog.h"
#include "virjson.h"
#include "virfile.h"
#include "virhash.h"
#include "virstring.h"
#include "viralloc.h"

#define VIR_FROM_THIS VIR_FROM_NETWORK

VIR_LOG_INIT("util.virmacmap");

/**
 * VIR_MAC_MAP_FILE_SIZE_MAX:
 *
 * Macro providing the upper limit on the size of mac maps file
 */
#define VIR_MAC_MAP_FILE_SIZE_MAX (32 * 1024 * 1024)

struct virMACMapMgr {
    virObjectLockable parent;

    virHashTablePtr macs;
};


static virClassPtr virMACMapMgrClass;


static void
virMACMapMgrDispose(void *obj)
{
    virMACMapMgrPtr mgr = obj;
    virHashFree(mgr->macs);
}


static void
virMACMapMgrHashFree(void *payload, const void *name ATTRIBUTE_UNUSED)
{
    virStringListFree(payload);
}


static int virMACMapMgrOnceInit(void)
{
    if (!(virMACMapMgrClass = virClassNew(virClassForObjectLockable(),
                                          "virMACMapMgrClass",
                                          sizeof(virMACMapMgr),
                                          virMACMapMgrDispose)))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(virMACMapMgr);


static int
virMACMapMgrAddLocked(virMACMapMgrPtr mgr,
                      const char *domain,
                      const char *mac)
{
    int ret = -1;
    const char **macsList = NULL;
    char **newMacsList = NULL;

    if ((macsList = virHashLookup(mgr->macs, domain)) &&
        virStringListHasString(macsList, mac)) {
        ret = 0;
        goto cleanup;
    }

    if (!(newMacsList = virStringListAdd(macsList, mac)) ||
        virHashUpdateEntry(mgr->macs, domain, newMacsList) < 0)
        goto cleanup;
    newMacsList = NULL;

    ret = 0;
 cleanup:
    virStringListFree(newMacsList);
    return ret;
}


static int
virMACMapMgrRemoveLocked(virMACMapMgrPtr mgr,
                         const char *domain,
                         const char *mac)
{
    const char **macsList = NULL;
    char **newMacsList = NULL;
    int ret = -1;
    int rv;

    if (!(macsList = virHashLookup(mgr->macs, domain)))
        return 0;

    if (!virStringListHasString(macsList, mac)) {
        ret = 0;
        goto cleanup;
    }

    rv = virStringListRemove(macsList, &newMacsList, mac);
    if (rv < 0) {
        goto cleanup;
    } else if (rv == 0) {
        virHashRemoveEntry(mgr->macs, domain);
    } else {
        if (virHashUpdateEntry(mgr->macs, domain, newMacsList) < 0)
            goto cleanup;
    }
    newMacsList = NULL;

    ret = 0;
 cleanup:
    virStringListFree(newMacsList);
    return ret;
}


static int
virMACMapMgrLoadFile(virMACMapMgrPtr mgr,
                     const char *file)
{
    char *map_str = NULL;
    virJSONValuePtr map = NULL;
    int map_str_len = 0;
    size_t i;
    int ret = -1;

    if (virFileExists(file) &&
        (map_str_len = virFileReadAll(file,
                                      VIR_MAC_MAP_FILE_SIZE_MAX,
                                      &map_str)) < 0)
        goto cleanup;

    if (map_str_len == 0) {
        ret = 0;
        goto cleanup;
    }

    if (!(map = virJSONValueFromString(map_str))) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("invalid json in file: %s"),
                       file);
        goto cleanup;
    }

    if (!virJSONValueIsArray(map)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Malformed file structure: %s"),
                       file);
        goto cleanup;
    }

    for (i = 0; i < virJSONValueArraySize(map); i++) {
        virJSONValuePtr tmp = virJSONValueArrayGet(map, i);
        virJSONValuePtr macs;
        const char *domain;
        size_t j;

        if (!(domain = virJSONValueObjectGetString(tmp, "domain"))) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Missing domain"));
            goto cleanup;
        }

        if (!(macs = virJSONValueObjectGetArray(tmp, "macs"))) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Missing macs"));
            goto cleanup;
        }

        for (j = 0; j < virJSONValueArraySize(macs); j++) {
            virJSONValuePtr macJSON = virJSONValueArrayGet(macs, j);
            const char *mac = virJSONValueGetString(macJSON);

            if (virMACMapMgrAddLocked(mgr, domain, mac) < 0)
                goto cleanup;
        }
    }

    ret = 0;
 cleanup:
    VIR_FREE(map_str);
    virJSONValueFree(map);
    return ret;
}


static int
virMACMapHashDumper(void *payload,
                    const void *name,
                    void *data)
{
    virJSONValuePtr obj = NULL;
    virJSONValuePtr arr = NULL;
    const char **macs = payload;
    size_t i;
    int ret = -1;

    if (!(obj = virJSONValueNewObject()) ||
        !(arr = virJSONValueNewArray()))
        goto cleanup;

    for (i = 0; macs[i]; i++) {
        virJSONValuePtr m = virJSONValueNewString(macs[i]);

        if (!m ||
            virJSONValueArrayAppend(arr, m) < 0) {
            virJSONValueFree(m);
            goto cleanup;
        }
    }

    if (virJSONValueObjectAppendString(obj, "domain", name) < 0 ||
        virJSONValueObjectAppend(obj, "macs", arr) < 0)
        goto cleanup;
    arr = NULL;

    if (virJSONValueArrayAppend(data, obj) < 0)
        goto cleanup;
    obj = NULL;

    ret = 0;
 cleanup:
    virJSONValueFree(obj);
    virJSONValueFree(arr);
    return ret;
}


static int
virMACMapMgrDumpStr(virMACMapMgrPtr mgr,
                    char **str)
{
    virJSONValuePtr arr;
    int ret = -1;

    if (!(arr = virJSONValueNewArray()))
        goto cleanup;

    if (virHashForEach(mgr->macs, virMACMapHashDumper, arr) < 0)
        goto cleanup;

    if (!(*str = virJSONValueToString(arr, true)))
        goto cleanup;

    ret = 0;
 cleanup:
    virJSONValueFree(arr);
    return ret;
}


static int
virMACMapMgrWriteFile(virMACMapMgrPtr mgr,
                      const char *file)
{
    char *str;
    int ret = -1;

    if (virMACMapMgrDumpStr(mgr, &str) < 0)
        goto cleanup;

    if (virFileRewriteStr(file, 0644, str) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    VIR_FREE(str);
    return ret;
}


#define VIR_MAC_HASH_TABLE_SIZE 10

virMACMapMgrPtr
virMACMapMgrNew(const char *file)
{
    virMACMapMgrPtr mgr;

    if (virMACMapMgrInitialize() < 0)
        return NULL;

    if (!(mgr = virObjectLockableNew(virMACMapMgrClass)))
        return NULL;

    virObjectLock(mgr);
    if (!(mgr->macs = virHashCreate(VIR_MAC_HASH_TABLE_SIZE,
                                    virMACMapMgrHashFree)))
        goto error;

    if (file &&
        virMACMapMgrLoadFile(mgr, file) < 0)
        goto error;

    virObjectUnlock(mgr);
    return mgr;

 error:
    virObjectUnlock(mgr);
    virObjectUnref(mgr);
    return NULL;
}


int
virMACMapMgrAdd(virMACMapMgrPtr mgr,
                const char *domain,
                const char *mac)
{
    int ret;

    virObjectLock(mgr);
    ret = virMACMapMgrAddLocked(mgr, domain, mac);
    virObjectUnlock(mgr);
    return ret;
}


int
virMACMapMgrRemove(virMACMapMgrPtr mgr,
                   const char *domain,
                   const char *mac)
{
    int ret;

    virObjectLock(mgr);
    ret = virMACMapMgrRemoveLocked(mgr, domain, mac);
    virObjectUnlock(mgr);
    return ret;
}


const char *const *
virMACMapMgrLookup(virMACMapMgrPtr mgr,
                   const char *domain)
{
    const char *const *ret;

    virObjectLock(mgr);
    ret = virHashLookup(mgr->macs, domain);
    virObjectUnlock(mgr);
    return ret;
}


int
virMACMapMgrFlush(virMACMapMgrPtr mgr,
                  const char *filename)
{
    int ret;

    virObjectLock(mgr);
    ret = virMACMapMgrWriteFile(mgr, filename);
    virObjectUnlock(mgr);
    return ret;
}


int
virMACMapMgrFlushStr(virMACMapMgrPtr mgr,
                     char **str)
{
    int ret;

    virObjectLock(mgr);
    ret = virMACMapMgrDumpStr(mgr, str);
    virObjectUnlock(mgr);
    return ret;
}
