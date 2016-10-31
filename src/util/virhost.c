/*
 * virscsi.c: helper APIs for managing scsi_host devices
 *
 * Copyright (C) 2016 IBM Corporation
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
 *     Eric Farman <farman@linux.vnet.ibm.com>
 */

#include <config.h>
#include <fcntl.h>

#include "virhost.h"
#include "virlog.h"
#include "viralloc.h"
#include "virerror.h"
#include "virfile.h"
#include "virstring.h"

VIR_LOG_INIT("util.host");

#define SYSFS_VHOST_SCSI_DEVICES "/sys/kernel/config/target/vhost/"
#define VHOST_SCSI_DEVICE "/dev/vhost-scsi"

struct _virUsedByInfo {
    char *drvname; /* which driver */
    char *domname; /* which domain */
};
typedef struct _virUsedByInfo *virUsedByInfoPtr;

struct _virHostDevice {
    char *name; /* naa.<wwn> */
    char *path;
    virUsedByInfoPtr *used_by; /* driver:domain(s) using this dev */
    size_t n_used_by; /* how many domains are using this dev */
};

struct _virHostDeviceList {
    virObjectLockable parent;
    size_t count;
    virHostDevicePtr *devs;
};

static virClassPtr virHostDeviceListClass;

static void
virHostDeviceListDispose(void *obj)
{
    virHostDeviceListPtr list = obj;
    size_t i;

    for (i = 0; i < list->count; i++)
        virHostDeviceFree(list->devs[i]);

    VIR_FREE(list->devs);
}


static int
virHostOnceInit(void)
{
    if (!(virHostDeviceListClass = virClassNew(virClassForObjectLockable(),
                                               "virHostDeviceList",
                                               sizeof(virHostDeviceList),
                                               virHostDeviceListDispose)))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(virHost)

/* For virReportOOMError()  and virReportSystemError() */
#define VIR_FROM_THIS VIR_FROM_NONE

int
virHostOpenVhostSCSI(int *vhostfd)
{
    if (!virFileExists(VHOST_SCSI_DEVICE))
        goto error;

    *vhostfd = open(VHOST_SCSI_DEVICE, O_RDWR);

    if (*vhostfd < 0) {
        virReportSystemError(errno, _("Failed to open %s"), VHOST_SCSI_DEVICE);
        goto error;
    }

    return 0;

 error:
    VIR_FORCE_CLOSE(*vhostfd);

    return -1;
}

static void
virHostDeviceUsedByInfoFree(virUsedByInfoPtr used_by)
{
    VIR_FREE(used_by->drvname);
    VIR_FREE(used_by->domname);
    VIR_FREE(used_by);
}

void
virHostDeviceListDel(virHostDeviceListPtr list,
                     virHostDevicePtr dev,
                     const char *drvname,
                     const char *domname)
{
    virHostDevicePtr tmp = NULL;
    size_t i;

    for (i = 0; i < dev->n_used_by; i++) {
        if (STREQ_NULLABLE(dev->used_by[i]->drvname, drvname) &&
            STREQ_NULLABLE(dev->used_by[i]->domname, domname)) {
            if (dev->n_used_by > 1) {
                virHostDeviceUsedByInfoFree(dev->used_by[i]);
                VIR_DELETE_ELEMENT(dev->used_by, i, dev->n_used_by);
            } else {
                tmp = virHostDeviceListSteal(list, dev);
                virHostDeviceFree(tmp);
            }
            break;
        }
    }
}

int
virHostDeviceListFindIndex(virHostDeviceListPtr list, virHostDevicePtr dev)
{
    size_t i;

    for (i = 0; i < list->count; i++) {
        virHostDevicePtr other = list->devs[i];
        if (STREQ_NULLABLE(other->name, dev->name))
            return i;
    }
    return -1;
}

virHostDevicePtr
virHostDeviceListGet(virHostDeviceListPtr list, int idx)
{
    if (idx >= list->count || idx < 0)
        return NULL;

    return list->devs[idx];
}

size_t
virHostDeviceListCount(virHostDeviceListPtr list)
{
    return list->count;
}

virHostDevicePtr
virHostDeviceListSteal(virHostDeviceListPtr list,
                       virHostDevicePtr dev)
{
    virHostDevicePtr ret = NULL;
    size_t i;

    for (i = 0; i < list->count; i++) {
        if (STREQ_NULLABLE(list->devs[i]->name, dev->name)) {
            ret = list->devs[i];
            VIR_DELETE_ELEMENT(list->devs, i, list->count);
            break;
        }
    }

    return ret;
}

virHostDevicePtr
virHostDeviceListFind(virHostDeviceListPtr list, virHostDevicePtr dev)
{
    int idx;

    if ((idx = virHostDeviceListFindIndex(list, dev)) >= 0)
        return list->devs[idx];
    else
        return NULL;
}

int
virHostDeviceListAdd(virHostDeviceListPtr list,
                     virHostDevicePtr dev)
{
    if (virHostDeviceListFind(list, dev)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Device %s is already in use"), dev->name);
        return -1;
    }
    return VIR_APPEND_ELEMENT(list->devs, list->count, dev);
}

virHostDeviceListPtr
virHostDeviceListNew(void)
{
    virHostDeviceListPtr list;

    if (virHostInitialize() < 0)
        return NULL;

    if (!(list = virObjectLockableNew(virHostDeviceListClass)))
        return NULL;

    return list;
}

int
virHostDeviceSetUsedBy(virHostDevicePtr dev,
                       const char *drvname,
                       const char *domname)
{
    virUsedByInfoPtr copy;
    if (VIR_ALLOC(copy) < 0)
        return -1;
    if (VIR_STRDUP(copy->drvname, drvname) < 0 ||
        VIR_STRDUP(copy->domname, domname) < 0)
        goto cleanup;

    if (VIR_APPEND_ELEMENT(dev->used_by, dev->n_used_by, copy) < 0)
        goto cleanup;

    return 0;

 cleanup:
    virHostDeviceUsedByInfoFree(copy);
    return -1;
}

int
virHostDeviceFileIterate(virHostDevicePtr dev,
                         virHostDeviceFileActor actor,
                         void *opaque)
{
    return (actor)(dev, dev->path, opaque);
}

const char *
virHostDeviceGetName(virHostDevicePtr dev)
{
    return dev->name;
}

virHostDevicePtr
virHostDeviceNew(const char *name)
{
    virHostDevicePtr dev;

    if (VIR_ALLOC(dev) < 0)
        return NULL;

    if (VIR_STRDUP(dev->name, name) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("dev->name buffer overflow: %s"),
                       name);
        goto error;
    }

    if (virAsprintf(&dev->path, "%s/%s",
                    SYSFS_VHOST_SCSI_DEVICES, name) < 0)
        goto cleanup;

    VIR_DEBUG("%s: initialized", dev->name);

 cleanup:
    return dev;

 error:
    virHostDeviceFree(dev);
    dev = NULL;
    goto cleanup;
}

void
virHostDeviceFree(virHostDevicePtr dev)
{
    if (!dev)
        return;
    VIR_DEBUG("%s: freeing", dev->name);
    VIR_FREE(dev);
}


