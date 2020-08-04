/*
 * virscsivhost.c: helper APIs for managing scsi_host devices
 *
 * Copyright (C) 2016 IBM Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>
#include <fcntl.h>

#include "virscsivhost.h"
#include "virlog.h"
#include "virerror.h"
#include "virfile.h"
#include "virstring.h"
#include "viralloc.h"

/* For virReportOOMError()  and virReportSystemError() */
#define VIR_FROM_THIS VIR_FROM_NONE

VIR_LOG_INIT("util.scsihost");

#define SYSFS_VHOST_SCSI_DEVICES "/sys/kernel/config/target/vhost/"
#define VHOST_SCSI_DEVICE "/dev/vhost-scsi"

struct _virSCSIVHostDevice {
    char *name; /* naa.<wwn> */
    char *path;
    char *used_by_drvname;
    char *used_by_domname;
};

struct _virSCSIVHostDeviceList {
    virObjectLockable parent;
    size_t count;
    virSCSIVHostDevicePtr *devs;
};

static virClassPtr virSCSIVHostDeviceListClass;

static void
virSCSIVHostDeviceListDispose(void *obj)
{
    virSCSIVHostDeviceListPtr list = obj;
    size_t i;

    for (i = 0; i < list->count; i++)
        virSCSIVHostDeviceFree(list->devs[i]);

    VIR_FREE(list->devs);
}


static int
virSCSIVHostOnceInit(void)
{
    if (!VIR_CLASS_NEW(virSCSIVHostDeviceList, virClassForObjectLockable()))
        return -1;

    return 0;
}


VIR_ONCE_GLOBAL_INIT(virSCSIVHost);


int
virSCSIVHostOpenVhostSCSI(int *vhostfd)
{
    if (!virFileExists(VHOST_SCSI_DEVICE)) {
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                       _("vhost-scsi device file '%s' cannot be found"),
                       VHOST_SCSI_DEVICE);
        return -1;
    }

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


void
virSCSIVHostDeviceListDel(virSCSIVHostDeviceListPtr list,
                          virSCSIVHostDevicePtr dev)
{
    virSCSIVHostDeviceFree(virSCSIVHostDeviceListSteal(list, dev));
}


static int
virSCSIVHostDeviceListFindIndex(virSCSIVHostDeviceListPtr list,
                                virSCSIVHostDevicePtr dev)
{
    size_t i;

    for (i = 0; i < list->count; i++) {
        virSCSIVHostDevicePtr other = list->devs[i];
        if (STREQ_NULLABLE(other->name, dev->name))
            return i;
    }
    return -1;
}


virSCSIVHostDevicePtr
virSCSIVHostDeviceListGet(virSCSIVHostDeviceListPtr list, int idx)
{
    if (idx >= list->count || idx < 0)
        return NULL;

    return list->devs[idx];
}


size_t
virSCSIVHostDeviceListCount(virSCSIVHostDeviceListPtr list)
{
    return list->count;
}


virSCSIVHostDevicePtr
virSCSIVHostDeviceListSteal(virSCSIVHostDeviceListPtr list,
                            virSCSIVHostDevicePtr dev)
{
    virSCSIVHostDevicePtr ret = NULL;
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


virSCSIVHostDevicePtr
virSCSIVHostDeviceListFind(virSCSIVHostDeviceListPtr list,
                           virSCSIVHostDevicePtr dev)
{
    int idx;

    if ((idx = virSCSIVHostDeviceListFindIndex(list, dev)) >= 0)
        return list->devs[idx];
    else
        return NULL;
}


int
virSCSIVHostDeviceListAdd(virSCSIVHostDeviceListPtr list,
                          virSCSIVHostDevicePtr dev)
{
    if (virSCSIVHostDeviceListFind(list, dev)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Device %s is already in use"), dev->name);
        return -1;
    }
    return VIR_APPEND_ELEMENT(list->devs, list->count, dev);
}


virSCSIVHostDeviceListPtr
virSCSIVHostDeviceListNew(void)
{
    if (virSCSIVHostInitialize() < 0)
        return NULL;

    return virObjectLockableNew(virSCSIVHostDeviceListClass);
}


int
virSCSIVHostDeviceSetUsedBy(virSCSIVHostDevicePtr dev,
                            const char *drvname,
                            const char *domname)
{
    VIR_FREE(dev->used_by_drvname);
    VIR_FREE(dev->used_by_domname);
    dev->used_by_drvname = g_strdup(drvname);
    dev->used_by_domname = g_strdup(domname);

    return 0;
}


void
virSCSIVHostDeviceGetUsedBy(virSCSIVHostDevicePtr dev,
                            const char **drv_name,
                            const char **dom_name)
{
    *drv_name = dev->used_by_drvname;
    *dom_name = dev->used_by_domname;
 }


int
virSCSIVHostDeviceFileIterate(virSCSIVHostDevicePtr dev,
                              virSCSIVHostDeviceFileActor actor,
                              void *opaque)
{
    return (actor)(dev, dev->path, opaque);
}


const char *
virSCSIVHostDeviceGetName(virSCSIVHostDevicePtr dev)
{
    return dev->name;
}


const char *
virSCSIVHostDeviceGetPath(virSCSIVHostDevicePtr dev)
{
    return dev->path;
}


virSCSIVHostDevicePtr
virSCSIVHostDeviceNew(const char *name)
{
    g_autoptr(virSCSIVHostDevice) dev = NULL;

    if (VIR_ALLOC(dev) < 0)
        return NULL;

    dev->name = g_strdup(name);

    dev->path = g_strdup_printf("%s/%s", SYSFS_VHOST_SCSI_DEVICES, name);

    VIR_DEBUG("%s: initialized", dev->name);

    return g_steal_pointer(&dev);
}


void
virSCSIVHostDeviceFree(virSCSIVHostDevicePtr dev)
{
    if (!dev)
        return;
    VIR_DEBUG("%s: freeing", dev->name);
    VIR_FREE(dev->name);
    VIR_FREE(dev->path);
    VIR_FREE(dev->used_by_drvname);
    VIR_FREE(dev->used_by_domname);
    VIR_FREE(dev);
}
