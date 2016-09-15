/*
 * fs_backend_dir.c: file system backend implementation
 * Author: Olga Krishtal <okrishtal@virtuozzo.com>
 *
 * Copyright (C) 2016 Parallels IP Holdings GmbH
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
 */

#include <config.h>

#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#include "virerror.h"
#include "fs_backend_dir.h"
#include "fs_conf.h"
#include "vircommand.h"
#include "viralloc.h"
#include "virxml.h"
#include "virfile.h"
#include "virlog.h"
#include "virstring.h"
#include "fdstream.h"
#include "stat-time.h"

#define VIR_FROM_THIS VIR_FROM_FSPOOL

VIR_LOG_INIT("fs.fs_backend_dir");

static int
virFSDirBuild(virConnectPtr conn ATTRIBUTE_UNUSED,
              virFSPoolObjPtr fspool,
              unsigned int flags)
{
    int ret = -1;
    char *parent = NULL;
    char *p = NULL;
    mode_t mode;
    unsigned int dir_create_flags;

    virCheckFlags(0, -1);

    if (VIR_STRDUP(parent, fspool->def->target.path) < 0)
        goto error;
    if (!(p = strrchr(parent, '/'))) {
        virReportError(VIR_ERR_INVALID_ARG,
                       _("path '%s' is not absolute"),
                       fspool->def->target.path);
        goto error;
    }

    if (p != parent) {
        /* assure all directories in the path prior to the final dir
         * exist, with default uid/gid/mode. */
        *p = '\0';
        if (virFileMakePath(parent) < 0) {
            virReportSystemError(errno, _("cannot create path '%s'"),
                                 parent);
            goto error;
        }
    }

    dir_create_flags = VIR_DIR_CREATE_ALLOW_EXIST;
    mode = fspool->def->target.perms.mode;

    if (mode == (mode_t) -1 &&
        (!virFileExists(fspool->def->target.path)))
        mode = VIR_FS_DEFAULT_POOL_PERM_MODE;

    /* Now create the final dir in the path with the uid/gid/mode
     * requested in the config. If the dir already exists, just set
     * the perms. */
    if (virDirCreate(fspool->def->target.path,
                     mode,
                     fspool->def->target.perms.uid,
                     fspool->def->target.perms.gid,
                     dir_create_flags) < 0)
        goto error;

    ret = 0;

 error:
    VIR_FREE(parent);
    return ret;
}

static int
virFSDirRefresh(virConnectPtr conn ATTRIBUTE_UNUSED,
                virFSPoolObjPtr fspool)
{
    DIR *dir;
    struct dirent *entry;
    virFSItemDefPtr item = NULL;
    struct statvfs sb;
    struct stat statbuf;
    int fd = 0;
    int ret = -1;

    if (virDirOpen(&dir, fspool->def->target.path) < 0)
        goto cleanup;

    while (virDirRead(dir, &entry, fspool->def->target.path) > 0) {
        if (virStringHasControlChars(entry->d_name)) {
            VIR_WARN("Ignoring control characters under '%s'",
                     fspool->def->target.path);
            continue;
        }

        if (VIR_ALLOC(item) < 0)
            goto cleanup;

        if (VIR_STRDUP(item->name, entry->d_name) < 0)
            goto cleanup;
        item->type  = VIR_FSITEM_DIR;
        if (virAsprintf(&item->target.path, "%s/%s",
                        fspool->def->target.path,
                        item->name) == -1)
            goto cleanup;

        if (VIR_STRDUP(item->key, item->target.path) < 0)
            goto cleanup;


        if (VIR_APPEND_ELEMENT(fspool->items.objs, fspool->items.count, item) < 0)
            goto cleanup;
    }


    if ((fd = open(fspool->def->target.path, O_RDONLY)) < 0) {
        virReportSystemError(errno,
                             _("cannot open path '%s'"),
                             fspool->def->target.path);
        goto cleanup;
    }

    if (fstat(fd, &statbuf) < 0) {
        virReportSystemError(errno,
                             _("cannot stat path '%s'"),
                             fspool->def->target.path);
        goto cleanup;
    }

    fspool->def->target.perms.mode = statbuf.st_mode & S_IRWXUGO;
    fspool->def->target.perms.uid = statbuf.st_uid;
    fspool->def->target.perms.gid = statbuf.st_gid;

    if (statvfs(fspool->def->target.path, &sb) < 0) {
        virReportSystemError(errno,
                             _("cannot statvfs path '%s'"),
                             fspool->def->target.path);
        goto cleanup;
    }

    fspool->def->capacity = ((unsigned long long)sb.f_blocks *
                           (unsigned long long)sb.f_frsize);
    fspool->def->available = ((unsigned long long)sb.f_bfree *
                            (unsigned long long)sb.f_frsize);
    fspool->def->allocation = fspool->def->capacity - fspool->def->available;

    ret = 0;

 cleanup:
    VIR_DIR_CLOSE(dir);
    VIR_FORCE_CLOSE(fd);
    virFSItemDefFree(item);
    if (ret < 0)
        virFSPoolObjClearItems(fspool);
    return ret;
}

static int
virFSDirDelete(virConnectPtr conn ATTRIBUTE_UNUSED,
               virFSPoolObjPtr fspool,
               unsigned int flags)
{
    virCheckFlags(0, -1);

    if (rmdir(fspool->def->target.path) < 0) {
        virReportSystemError(errno, _("failed to remove fspool '%s'"),
                             fspool->def->target.path);
        return -1;
    }

    return 0;

}
static int
virFSDirItemBuild(virConnectPtr conn ATTRIBUTE_UNUSED,
                  virFSPoolObjPtr fspool ATTRIBUTE_UNUSED,
                  virFSItemDefPtr item,
                  unsigned int flags)
{
    virCheckFlags(0, -1);

    if (item->type == VIR_FSITEM_DIR) {
        if ((virDirCreate(item->target.path,
                          (item->target.perms->mode == (mode_t) -1 ?
                           VIR_FS_DEFAULT_ITEM_PERM_MODE:
                           item->target.perms->mode),
                          item->target.perms->uid,
                          item->target.perms->gid,
                          0)) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("error creating item"));
            return -1;
        }
    }

    return 0;
}

static int
virFSDirItemBuildFrom(virConnectPtr conn ATTRIBUTE_UNUSED,
                      virFSPoolObjPtr fspool ATTRIBUTE_UNUSED,
                      virFSItemDefPtr item,
                      virFSItemDefPtr inputitem,
                      unsigned int flags)
{
    virCommandPtr cmd = NULL;
    int ret = -1;

    virCheckFlags(0, -1);

    item->target.capacity = inputitem->target.capacity;
    cmd = virCommandNewArgList("cp", "-r", inputitem->target.path,
                               item->target.path, NULL);
    ret = virCommandRun(cmd, NULL);

    virCommandFree(cmd);
    return ret;
}

static int
virFSDirItemCreate(virConnectPtr conn ATTRIBUTE_UNUSED,
                   virFSPoolObjPtr fspool,
                   virFSItemDefPtr item)
{
    if (strchr(item->name, '/')) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("volume name '%s' cannot contain '/'"), item->name);
        return -1;
    }

    VIR_FREE(item->target.path);
    if (virAsprintf(&item->target.path, "%s/%s",
                    fspool->def->target.path,
                    item->name) == -1)
        return -1;

    if (virFileExists(item->target.path)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       _("item target path '%s' already exists"),
                       item->target.path);
        return -1;
    }

    VIR_FREE(item->key);
    return VIR_STRDUP(item->key, item->target.path);
}


static int
virFSDirItemRefresh(virConnectPtr conn ATTRIBUTE_UNUSED,
                    virFSPoolObjPtr fspool ATTRIBUTE_UNUSED,
                    virFSItemDefPtr item)
{
    int fd;
    int ret = -1;
    struct stat statbuf;
    virCommandPtr cmd = NULL;
    char *output = NULL, *end;

    if ((fd = open(item->target.path, O_RDONLY)) < 0) {
        virReportSystemError(errno, _("cannot open directory '%s'"),
                             item->target.path);
        return -1;
    }
    if (fstat(fd, &statbuf) < 0) {
         virReportSystemError(errno, _("cannot stat path '%s'"),
                             item->target.path);
        goto cleanup;
    }

    cmd = virCommandNewArgList("du", "-sB1", item->target.path, NULL);
    virCommandSetOutputBuffer(cmd, &output);
    if ((ret = virCommandRun(cmd, NULL)) < 0)
        goto cleanup;

    if (virStrToLong_ull(output, &end, 10, &item->target.allocation) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Malformed du output: %s"), output);
        goto cleanup;
    }

    if (&(item->target.perms) && VIR_ALLOC(*(&item->target.perms)) < 0)
            goto cleanup;
    item->target.perms->mode = statbuf.st_mode & S_IRWXUGO;
    item->target.perms->uid = statbuf.st_uid;
    item->target.perms->gid = statbuf.st_gid;

    ret = 0;
 cleanup:
    VIR_FORCE_CLOSE(fd);
    VIR_FREE(output);
    virCommandFree(cmd);
    return ret;
}

static int
virFSDirItemDelete(virConnectPtr conn ATTRIBUTE_UNUSED,
                   virFSPoolObjPtr fspool ATTRIBUTE_UNUSED,
                   virFSItemDefPtr item,
                   unsigned int flags)
{
    virCheckFlags(0, -1);

    return virFileDeleteTree(item->target.path);
}

virFSBackend virFSBackendDir = {
    .type = VIR_FSPOOL_DIR,

    .buildFSpool = virFSDirBuild,
    .refreshFSpool = virFSDirRefresh,
    .deleteFSpool = virFSDirDelete,
    .buildItem = virFSDirItemBuild,
    .buildItemFrom = virFSDirItemBuildFrom,
    .createItem = virFSDirItemCreate,
    .deleteItem = virFSDirItemDelete,
    .refreshItem = virFSDirItemRefresh,
};
