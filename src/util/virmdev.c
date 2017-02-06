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
 *
 * Authors:
 *     Erik Skultety <eskultet@redhat.com>
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
    char *path;             /* sysfs path */
    bool managed;

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
# define MDEV_SYSFS "/sys/class/mdev_bus/"

virMediatedDevicePtr
virMediatedDeviceNew(virPCIDeviceAddressPtr pciaddr, const char *uuidstr)
{
    virMediatedDevicePtr dev = NULL, ret = NULL;
    char *pcistr = NULL;

    if (virAsprintf(&pcistr, "%.4x:%.2x:%.2x.%.1x", pciaddr->domain,
                    pciaddr->bus, pciaddr->slot, pciaddr->function) < 0)
        return NULL;

    if (VIR_ALLOC(dev) < 0)
        goto cleanup;

    if (virAsprintf(&dev->path, MDEV_SYSFS "%s/%s", pcistr, uuidstr) < 0)
       goto cleanup;

    ret = dev;
    dev = NULL;

 cleanup:
    VIR_FREE(pcistr);
    virMediatedDeviceFree(dev);
    return ret;
}


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
                           char **drvname, char **domname)
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


#else
static const char *unsupported = N_("not supported on non-linux platforms");

virMediatedDevicePtr
virMediatedDeviceNew(virPCIDeviceAddressPtr pciaddr ATTRIBUTE_UNUSED,
                     const char *uuidstr ATTRIBUTE_UNUSED)
{
    virRerportError(VIR_ERR_INTERNAL_ERROR, "%s", _(unsupported));
    return NULL;
}


char *
virMediatedDeviceGetIOMMUGroupDev(virMediatedDevicePtr dev ATTRIBUTE_UNUSED)
{
    virRerportError(VIR_ERR_INTERNAL_ERROR, "%s", _(unsupported));
    return NULL;
}


void
virMediatedDeviceFree(virMediatedDevicePtr dev ATTRIBUTE_UNUSED)
{
    virRerportError(VIR_ERR_INTERNAL_ERROR, "%s", _(unsupported));
}


const char *
virMediatedDeviceGetPath(virMediatedDevicePtr dev ATTRIBUTE_UNUSED)
{
    virRerportError(VIR_ERR_INTERNAL_ERROR, "%s", _(unsupported));
    return NULL;
}


void
virMediatedDeviceGetUsedBy(virMediatedDevicePtr dev,
                           char **drvname, char **domname)
{
    virRerportError(VIR_ERR_INTERNAL_ERROR, "%s", _(unsupported));
    *drvname = NULL;
    *domname = NULL;
}


int
virMediatedDeviceSetUsedBy(virMediatedDevicePtr dev ATTRIBUTE_UNUSED,
                           const char *drvname ATTRIBUTE_UNUSED,
                           const char *domname ATTRIBUTE_UNUSED)
{
    virRerportError(VIR_ERR_INTERNAL_ERROR, "%s", _(unsupported));
    return -1;
}
#endif /* __linux__ */

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
                         int idx)
{
    if (idx >= list->count)
        return NULL;
    if (idx < 0)
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
                                int idx)
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
