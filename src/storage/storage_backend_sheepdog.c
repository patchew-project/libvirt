/*
 * storage_backend_sheepdog.c: storage backend for Sheepdog handling
 *
 * Copyright (C) 2013-2014 Red Hat, Inc.
 * Copyright (C) 2012 Wido den Hollander
 * Copyright (C) 2012 Frank Spijkerman
 * Copyright (C) 2012 Sebastian Wiedenroth
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
 * Author: Wido den Hollander <wido@widodh.nl>
 *         Frank Spijkerman <frank.spijkerman@avira.com>
 *         Sebastian Wiedenroth <sebastian.wiedenroth@skylime.net>
 */

#include <config.h>

#include "virerror.h"
#include "storage_backend_sheepdog.h"
#include "storage_backend_sheepdog_priv.h"
#include "vircommand.h"
#include "viralloc.h"
#include "virstring.h"
#include "storage_util.h"

#define VIR_FROM_THIS VIR_FROM_STORAGE

static int virStorageBackendSheepdogRefreshVol(virConnectPtr conn,
                                               virPoolObjPtr poolobj,
                                               virStorageVolDefPtr vol);

void virStorageBackendSheepdogAddHostArg(virCommandPtr cmd,
                                         virPoolObjPtr poolobj);

int
virStorageBackendSheepdogParseNodeInfo(virStoragePoolDefPtr def,
                                       char *output)
{
    /* fields:
     * node id/total, size, used, use%, [total vdi size]
     *
     * example output:
     * 0 15245667872 117571104 0%
     * Total 15245667872 117571104 0% 20972341
     */

    const char *p, *next;

    def->allocation = def->capacity = def->available = 0;

    p = output;
    do {
        char *end;

        if ((next = strchr(p, '\n')))
            ++next;
        else
            break;

        if (!STRPREFIX(p, "Total "))
            continue;

        p = p + 6;

        if (virStrToLong_ull(p, &end, 10, &def->capacity) < 0)
            break;

        if ((p = end + 1) > next)
            break;

        if (virStrToLong_ull(p, &end, 10, &def->allocation) < 0)
            break;

        def->available = def->capacity - def->allocation;
        return 0;

    } while ((p = next));

    return -1;
}

void
virStorageBackendSheepdogAddHostArg(virCommandPtr cmd,
                                    virPoolObjPtr poolobj)
{
    virStoragePoolDefPtr def = virPoolObjGetDef(poolobj);
    const char *address = "localhost";
    int port = 7000;
    if (def->source.nhost > 0) {
        if (def->source.hosts[0].name != NULL)
            address = def->source.hosts[0].name;
        if (def->source.hosts[0].port)
            port = def->source.hosts[0].port;
    }
    virCommandAddArg(cmd, "-a");
    virCommandAddArgFormat(cmd, "%s", address);
    virCommandAddArg(cmd, "-p");
    virCommandAddArgFormat(cmd, "%d", port);
}

static int
virStorageBackendSheepdogAddVolume(virConnectPtr conn ATTRIBUTE_UNUSED,
                                   virPoolObjPtr poolobj,
                                   const char *diskInfo)
{
    virPoolObjPtr volobj;
    virStorageVolDefPtr voldef = NULL;

    if (diskInfo == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Missing disk info when adding volume"));
        goto error;
    }

    if (VIR_ALLOC(voldef) < 0 || VIR_STRDUP(voldef->name, diskInfo) < 0)
        goto error;

    voldef->type = VIR_STORAGE_VOL_NETWORK;

    if (virStorageBackendSheepdogRefreshVol(conn, poolobj, voldef) < 0)
        goto error;

    if (!(volobj = virStoragePoolObjAddVolume(poolobj, voldef)))
        goto error;
    voldef = NULL;

    virPoolObjEndAPI(&volobj);
    return 0;

 error:
    virStorageVolDefFree(voldef);
    return -1;
}

static int
virStorageBackendSheepdogRefreshAllVol(virConnectPtr conn ATTRIBUTE_UNUSED,
                                       virPoolObjPtr poolobj)
{
    int ret = -1;
    char *output = NULL;
    char **lines = NULL;
    char **cells = NULL;
    size_t i;

    virCommandPtr cmd = virCommandNewArgList(SHEEPDOGCLI, "vdi", "list", "-r", NULL);
    virStorageBackendSheepdogAddHostArg(cmd, poolobj);
    virCommandSetOutputBuffer(cmd, &output);
    if (virCommandRun(cmd, NULL) < 0)
        goto cleanup;

    lines = virStringSplit(output, "\n", 0);
    if (lines == NULL)
        goto cleanup;

    for (i = 0; lines[i]; i++) {
        const char *line = lines[i];
        if (line == NULL)
            break;

        cells = virStringSplit(line, " ", 0);

        if (cells != NULL &&
            virStringListLength((const char * const *)cells) > 2) {
            if (virStorageBackendSheepdogAddVolume(conn, poolobj,
                                                   cells[1]) < 0)
                goto cleanup;
        }

        virStringListFree(cells);
        cells = NULL;
    }

    ret = 0;

 cleanup:
    virCommandFree(cmd);
    virStringListFree(lines);
    virStringListFree(cells);
    VIR_FREE(output);
    return ret;
}


static int
virStorageBackendSheepdogRefreshPool(virConnectPtr conn ATTRIBUTE_UNUSED,
                                     virPoolObjPtr poolobj)
{
    virStoragePoolDefPtr def = virPoolObjGetDef(poolobj);
    int ret = -1;
    char *output = NULL;
    virCommandPtr cmd;

    cmd = virCommandNewArgList(SHEEPDOGCLI, "node", "info", "-r", NULL);
    virStorageBackendSheepdogAddHostArg(cmd, poolobj);
    virCommandSetOutputBuffer(cmd, &output);
    if (virCommandRun(cmd, NULL) < 0)
        goto cleanup;

    if (virStorageBackendSheepdogParseNodeInfo(def, output) < 0)
        goto cleanup;

    ret = virStorageBackendSheepdogRefreshAllVol(conn, poolobj);
 cleanup:
    virCommandFree(cmd);
    VIR_FREE(output);
    return ret;
}


static int
virStorageBackendSheepdogDeleteVol(virConnectPtr conn ATTRIBUTE_UNUSED,
                                   virPoolObjPtr poolobj,
                                   virStorageVolDefPtr vol,
                                   unsigned int flags)
{

    virCheckFlags(0, -1);

    virCommandPtr cmd = virCommandNewArgList(SHEEPDOGCLI, "vdi", "delete", vol->name, NULL);
    virStorageBackendSheepdogAddHostArg(cmd, poolobj);
    int ret = virCommandRun(cmd, NULL);

    virCommandFree(cmd);
    return ret;
}


static int
virStorageBackendSheepdogCreateVol(virConnectPtr conn ATTRIBUTE_UNUSED,
                                   virPoolObjPtr poolobj,
                                   virStorageVolDefPtr vol)
{
    virStoragePoolDefPtr def = virPoolObjGetDef(poolobj);

    if (vol->target.encryption != NULL) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       "%s", _("storage pool does not support encrypted "
                               "volumes"));
        return -1;
    }

    vol->type = VIR_STORAGE_VOL_NETWORK;

    VIR_FREE(vol->key);
    if (virAsprintf(&vol->key, "%s/%s",
                    def->source.name, vol->name) == -1)
        return -1;

    VIR_FREE(vol->target.path);
    if (VIR_STRDUP(vol->target.path, vol->name) < 0)
        return -1;

    return 0;
}


static int
virStorageBackendSheepdogBuildVol(virConnectPtr conn ATTRIBUTE_UNUSED,
                                  virPoolObjPtr poolobj,
                                  virStorageVolDefPtr vol,
                                  unsigned int flags)
{
    int ret = -1;
    virCommandPtr cmd = NULL;

    virCheckFlags(0, -1);

    if (!vol->target.capacity) {
        virReportError(VIR_ERR_NO_SUPPORT, "%s",
                       _("volume capacity required for this pool"));
        goto cleanup;
    }

    cmd = virCommandNewArgList(SHEEPDOGCLI, "vdi", "create", vol->name, NULL);
    virCommandAddArgFormat(cmd, "%llu", vol->target.capacity);
    virStorageBackendSheepdogAddHostArg(cmd, poolobj);
    if (virCommandRun(cmd, NULL) < 0)
        goto cleanup;

    ret = 0;
 cleanup:
    virCommandFree(cmd);
    return ret;
}


int
virStorageBackendSheepdogParseVdiList(virStorageVolDefPtr vol,
                                      char *output)
{
    /* fields:
     * current/clone/snapshot, name, id, size, used, shared, creation time, vdi id, [tag]
     *
     * example output:
     * s test 1 10 0 0 1336556634 7c2b25
     * s test 2 10 0 0 1336557203 7c2b26
     * = test 3 10 0 0 1336557216 7c2b27
     */

    int id;
    const char *p, *next;

    vol->target.allocation = vol->target.capacity = 0;

    p = output;
    do {
        char *end;

        if ((next = strchr(p, '\n')))
            ++next;

        /* ignore snapshots */
        if (*p != '=')
            continue;

        /* skip space */
        if (p + 2 < next)
            p += 2;
        else
            return -1;

        /* skip name */
        while (*p != '\0' && *p != ' ') {
            if (*p == '\\')
                ++p;
            ++p;
        }

        if (virStrToLong_i(p, &end, 10, &id) < 0)
            return -1;

        p = end + 1;

        if (virStrToLong_ull(p, &end, 10, &vol->target.capacity) < 0)
            return -1;

        p = end + 1;

        if (virStrToLong_ull(p, &end, 10, &vol->target.allocation) < 0)
            return -1;

        return 0;
    } while ((p = next));

    return -1;
}

static int
virStorageBackendSheepdogRefreshVol(virConnectPtr conn ATTRIBUTE_UNUSED,
                                    virPoolObjPtr poolobj,
                                    virStorageVolDefPtr vol)
{
    virStoragePoolDefPtr def = virPoolObjGetDef(poolobj);
    int ret;
    char *output = NULL;

    virCommandPtr cmd = virCommandNewArgList(SHEEPDOGCLI, "vdi", "list", vol->name, "-r", NULL);
    virStorageBackendSheepdogAddHostArg(cmd, poolobj);
    virCommandSetOutputBuffer(cmd, &output);
    ret = virCommandRun(cmd, NULL);

    if (ret < 0)
        goto cleanup;

    if ((ret = virStorageBackendSheepdogParseVdiList(vol, output)) < 0)
        goto cleanup;

    vol->type = VIR_STORAGE_VOL_NETWORK;

    VIR_FREE(vol->key);
    if (virAsprintf(&vol->key, "%s/%s",
                    def->source.name, vol->name) == -1)
        goto cleanup;

    VIR_FREE(vol->target.path);
    ignore_value(VIR_STRDUP(vol->target.path, vol->name));
 cleanup:
    virCommandFree(cmd);
    return ret;
}


static int
virStorageBackendSheepdogResizeVol(virConnectPtr conn ATTRIBUTE_UNUSED,
                                   virPoolObjPtr poolobj,
                                   virStorageVolDefPtr vol,
                                   unsigned long long capacity,
                                   unsigned int flags)
{

    virCheckFlags(0, -1);

    virCommandPtr cmd = virCommandNewArgList(SHEEPDOGCLI, "vdi", "resize", vol->name, NULL);
    virCommandAddArgFormat(cmd, "%llu", capacity);
    virStorageBackendSheepdogAddHostArg(cmd, poolobj);
    int ret = virCommandRun(cmd, NULL);

    virCommandFree(cmd);
    return ret;

}



virStorageBackend virStorageBackendSheepdog = {
    .type = VIR_STORAGE_POOL_SHEEPDOG,

    .refreshPool = virStorageBackendSheepdogRefreshPool,
    .createVol = virStorageBackendSheepdogCreateVol,
    .buildVol = virStorageBackendSheepdogBuildVol,
    .refreshVol = virStorageBackendSheepdogRefreshVol,
    .deleteVol = virStorageBackendSheepdogDeleteVol,
    .resizeVol = virStorageBackendSheepdogResizeVol,
};
