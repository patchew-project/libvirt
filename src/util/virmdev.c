/*
 * virmdev.c: helper APIs for managing host MDEV devices
 *
 * Copyright (C) 2017-2018 Red Hat, Inc.
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

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#include "virmdev.h"
#include "dirname.h"
#include "virlog.h"
#include "viralloc.h"
#include "vircommand.h"
#include "virerror.h"
#include "virfile.h"
#include "virkmod.h"
#include "virstring.h"
#include "virutil.h"
#include "viruuid.h"
#include "virhostdev.h"

VIR_LOG_INIT("util.mdev");

struct _virMediatedDevice {
    char *path;                             /* sysfs path */

    char *used_by_drvname;
    char *used_by_domname;
};

struct _virMediatedDeviceList {
    virObjectLockable parent;

    size_t count;
    virMediatedDevicePtr *devs;
};


/* For virReportOOMError()  and virReportSystemError() */
#define VIR_FROM_THIS VIR_FROM_NONE

static virClassPtr virMediatedDeviceListClass;

static void virMediatedDeviceListDispose(void *obj);

static int virMediatedOnceInit(void)
{
    if (!(virMediatedDeviceListClass = virClassNew(virClassForObjectLockable(),
                                                   "virMediatedDeviceList",
                                                   sizeof(virMediatedDeviceList),
                                                   virMediatedDeviceListDispose)))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(virMediated)

#ifdef __linux__
# define MDEV_SYSFS_DEVICES "/sys/bus/mdev/devices/"

virMediatedDevicePtr
virMediatedDeviceNew(const char *uuidstr)
{
    virMediatedDevicePtr dev = NULL, ret = NULL;

    if (VIR_ALLOC(dev) < 0)
        return NULL;

    if (virAsprintf(&dev->path, MDEV_SYSFS_DEVICES "%s", uuidstr) < 0)
       goto cleanup;

    ret = dev;
    dev = NULL;

 cleanup:
    virMediatedDeviceFree(dev);
    return ret;
}

#else

virMediatedDevicePtr
virMediatedDeviceNew(virPCIDeviceAddressPtr pciaddr ATTRIBUTE_UNUSED,
                     const char *uuidstr ATTRIBUTE_UNUSED)
{
    virRerportError(VIR_ERR_INTERNAL_ERROR, "%s",
                    _("not supported on non-linux platforms"));
    return NULL;
}

#endif /* __linux__ */

void
virMediatedDeviceFree(virMediatedDevicePtr dev)
{
    if (!dev)
        return;
    VIR_FREE(dev->path);
    VIR_FREE(dev->used_by_drvname);
    VIR_FREE(dev->used_by_domname);
    VIR_FREE(dev);
}


const char *
virMediatedDeviceGetPath(virMediatedDevicePtr dev)
{
    return dev->path;
}


/* Returns an absolute canonicalized path to the device used to control the
 * mediated device's IOMMU group (e.g. "/dev/vfio/15")
 */
char *
virMediatedDeviceGetIOMMUGroupDev(virMediatedDevicePtr dev)
{
    char *resultpath = NULL;
    char *iommu_linkpath = NULL;
    char *vfio_path = NULL;

    if (virAsprintf(&iommu_linkpath, "%s/iommu_group", dev->path) < 0)
        return NULL;

    if (virFileIsLink(iommu_linkpath) != 1) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("IOMMU group file %s is not a symlink"),
                       iommu_linkpath);
        goto cleanup;
    }

    if (virFileResolveLink(iommu_linkpath, &resultpath) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Unable to resolve IOMMU group symlink %s"),
                       iommu_linkpath);
        goto cleanup;
    }

    if (virAsprintf(&vfio_path, "/dev/vfio/%s", last_component(resultpath)) < 0)
        goto cleanup;

 cleanup:
    VIR_FREE(resultpath);
    VIR_FREE(iommu_linkpath);
    return vfio_path;
}


void
virMediatedDeviceGetUsedBy(virMediatedDevicePtr dev,
                           const char **drvname, const char **domname)
{
    *drvname = dev->used_by_drvname;
    *domname = dev->used_by_domname;
}


int
virMediatedDeviceSetUsedBy(virMediatedDevicePtr dev,
                           const char *drvname,
                           const char *domname)
{
    VIR_FREE(dev->used_by_drvname);
    VIR_FREE(dev->used_by_domname);
    if (VIR_STRDUP(dev->used_by_drvname, drvname) < 0)
        return -1;
    if (VIR_STRDUP(dev->used_by_domname, domname) < 0)
        return -1;

    return 0;
}


virMediatedDeviceListPtr
virMediatedDeviceListNew(void)
{
    virMediatedDeviceListPtr list;

    if (virMediatedInitialize() < 0)
        return NULL;

    if (!(list = virObjectLockableNew(virMediatedDeviceListClass)))
        return NULL;

    return list;
}


static void
virMediatedDeviceListDispose(void *obj)
{
    virMediatedDeviceListPtr list = obj;
    size_t i;

    for (i = 0; i < list->count; i++) {
        virMediatedDeviceFree(list->devs[i]);
        list->devs[i] = NULL;
    }

    list->count = 0;
    VIR_FREE(list->devs);
}


int
virMediatedDeviceListAdd(virMediatedDeviceListPtr list,
                         virMediatedDevicePtr dev)
{
    if (virMediatedDeviceListFind(list, dev)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Device %s is already in use"), dev->path);
        return -1;
    }
    return VIR_APPEND_ELEMENT(list->devs, list->count, dev);
}


virMediatedDevicePtr
virMediatedDeviceListGet(virMediatedDeviceListPtr list,
                         ssize_t idx)
{
    if (idx < 0 || idx >= list->count)
        return NULL;

    return list->devs[idx];
}


size_t
virMediatedDeviceListCount(virMediatedDeviceListPtr list)
{
    return list->count;
}


virMediatedDevicePtr
virMediatedDeviceListStealIndex(virMediatedDeviceListPtr list,
                                ssize_t idx)
{
    virMediatedDevicePtr ret;

    if (idx < 0 || idx >= list->count)
        return NULL;

    ret = list->devs[idx];
    VIR_DELETE_ELEMENT(list->devs, idx, list->count);
    return ret;
}


virMediatedDevicePtr
virMediatedDeviceListSteal(virMediatedDeviceListPtr list,
                           virMediatedDevicePtr dev)
{
    int idx = virMediatedDeviceListFindIndex(list, dev);

    return virMediatedDeviceListStealIndex(list, idx);
}


void
virMediatedDeviceListDel(virMediatedDeviceListPtr list,
                         virMediatedDevicePtr dev)
{
    virMediatedDevicePtr ret = virMediatedDeviceListSteal(list, dev);
    virMediatedDeviceFree(ret);
}


int
virMediatedDeviceListFindIndex(virMediatedDeviceListPtr list,
                               virMediatedDevicePtr dev)
{
    size_t i;

    for (i = 0; i < list->count; i++) {
        virMediatedDevicePtr other = list->devs[i];
        if (STREQ(other->path, dev->path))
            return i;
    }
    return -1;
}


virMediatedDevicePtr
virMediatedDeviceListFind(virMediatedDeviceListPtr list,
                          virMediatedDevicePtr dev)
{
    int idx;

    if ((idx = virMediatedDeviceListFindIndex(list, dev)) >= 0)
        return list->devs[idx];
    else
        return NULL;
}


int
virMediatedDeviceGetDeviceAPI(virMediatedDevicePtr dev,
                              char **device_api)
{
    int ret = -1;
    char *buf = NULL;
    char *tmp = NULL;
    char *sysfs_path = NULL;

    if (virAsprintf(&sysfs_path, "%s/mdev_type/device_api", dev->path) < 0)
        goto cleanup;

    /* TODO - make this a generic method to access sysfs files for various
     * kinds of devices
     */
    if (!virFileExists(sysfs_path)) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("mediated devices are not supported by this kernel"));
        goto cleanup;
    }

    if (virFileReadAll(sysfs_path, 1024, &buf) < 0)
        goto cleanup;

    if ((tmp = strchr(buf, '\n')))
        *tmp = '\0';

    *device_api = buf;
    buf = NULL;

    ret = 0;
 cleanup:
    VIR_FREE(sysfs_path);
    VIR_FREE(buf);
    return ret;
}
