/*
 * virnvme.c: helper APIs for managing NVMe devices
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

#include "virnvme.h"
#include "virobject.h"
#include "virpci.h"
#include "viralloc.h"
#include "virlog.h"
#include "virstring.h"

VIR_LOG_INIT("util.pci");
#define VIR_FROM_THIS VIR_FROM_NONE

struct _virNVMeDevice {
    virPCIDeviceAddress address; /* PCI address of controller */
    unsigned long namespace; /* Namespace ID */
    bool managed;

    char *drvname;
    char *domname;
};


struct _virNVMeDeviceList {
    virObjectLockable parent;

    size_t count;
    virNVMeDevicePtr *devs;
};


static virClassPtr virNVMeDeviceListClass;

static void virNVMeDeviceListDispose(void *obj);

static int
virNVMeOnceInit(void)
{
    if (!VIR_CLASS_NEW(virNVMeDeviceList, virClassForObjectLockable()))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(virNVMe);


virNVMeDevicePtr
virNVMeDeviceNew(const virPCIDeviceAddress *address,
                 unsigned long namespace,
                 bool managed)
{
    VIR_AUTOPTR(virNVMeDevice) dev = NULL;

    if (VIR_ALLOC(dev) < 0)
        return NULL;

    virPCIDeviceAddressCopy(&dev->address, address);
    dev->namespace = namespace;
    dev->managed = managed;

    VIR_RETURN_PTR(dev);
}


void
virNVMeDeviceFree(virNVMeDevicePtr dev)
{
    if (!dev)
        return;

    virNVMeDeviceUsedByClear(dev);
    VIR_FREE(dev);
}


virNVMeDevicePtr
virNVMeDeviceCopy(const virNVMeDevice *dev)
{
    VIR_AUTOPTR(virNVMeDevice) copy = NULL;

    if (VIR_ALLOC(copy) < 0 ||
        VIR_STRDUP(copy->drvname, dev->drvname) < 0 ||
        VIR_STRDUP(copy->domname, dev->domname) < 0)
        return NULL;

    virPCIDeviceAddressCopy(&copy->address, &dev->address);
    copy->namespace = dev->namespace;
    copy->managed = dev->managed;

    VIR_RETURN_PTR(copy);
}


const virPCIDeviceAddress *
virNVMeDeviceAddressGet(const virNVMeDevice *dev)
{
    return &dev->address;
}


void
virNVMeDeviceUsedByClear(virNVMeDevicePtr dev)
{
    VIR_FREE(dev->drvname);
    VIR_FREE(dev->domname);
}


void
virNVMeDeviceUsedByGet(const virNVMeDevice *dev,
                       const char **drv,
                       const char **dom)
{
    *drv = dev->drvname;
    *dom = dev->domname;
}


int
virNVMeDeviceUsedBySet(virNVMeDevicePtr dev,
                       const char *drv,
                       const char *dom)
{
    if (VIR_STRDUP(dev->drvname, drv) < 0 ||
        VIR_STRDUP(dev->domname, dom) < 0) {
        virNVMeDeviceUsedByClear(dev);
        return -1;
    }

    return 0;
}


virNVMeDeviceListPtr
virNVMeDeviceListNew(void)
{
    virNVMeDeviceListPtr list;

    if (virNVMeInitialize() < 0)
        return NULL;

    if (!(list = virObjectLockableNew(virNVMeDeviceListClass)))
        return NULL;

    return list;
}


static void
virNVMeDeviceListDispose(void *obj)
{
    virNVMeDeviceListPtr list = obj;
    size_t i;

    for (i = 0; i < list->count; i++)
        virNVMeDeviceFree(list->devs[i]);

    VIR_FREE(list->devs);
}


size_t
virNVMeDeviceListCount(const virNVMeDeviceList *list)
{
    return list->count;
}


int
virNVMeDeviceListAdd(virNVMeDeviceListPtr list,
                     const virNVMeDevice *dev)
{
    virNVMeDevicePtr tmp;

    if ((tmp = virNVMeDeviceListLookup(list, dev))) {
        VIR_AUTOFREE(char *) addrStr = virPCIDeviceAddressAsString(&tmp->address);
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("NVMe device %s namespace %lu is already on the list"),
                       NULLSTR(addrStr), tmp->namespace);
        return -1;
    }

    if (!(tmp = virNVMeDeviceCopy(dev)) ||
        VIR_APPEND_ELEMENT(list->devs, list->count, tmp) < 0) {
        virNVMeDeviceFree(tmp);
        return -1;
    }

    return 0;
}


int
virNVMeDeviceListDel(virNVMeDeviceListPtr list,
                     const virNVMeDevice *dev)
{
    ssize_t idx;
    virNVMeDevicePtr tmp = NULL;

    if ((idx = virNVMeDeviceListLookupIndex(list, dev)) < 0) {
        VIR_AUTOFREE(char *) addrStr = virPCIDeviceAddressAsString(&dev->address);
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("NVMe device %s namespace %lu not found"),
                       NULLSTR(addrStr), dev->namespace);
        return -1;
    }

    tmp = list->devs[idx];
    VIR_DELETE_ELEMENT(list->devs, idx, list->count);
    virNVMeDeviceFree(tmp);
    return 0;
}


virNVMeDevicePtr
virNVMeDeviceListGet(virNVMeDeviceListPtr list,
                     size_t i)
{
    return i < list->count ? list->devs[i] : NULL;
}


virNVMeDevicePtr
virNVMeDeviceListLookup(virNVMeDeviceListPtr list,
                        const virNVMeDevice *dev)
{
    ssize_t idx;

    if ((idx = virNVMeDeviceListLookupIndex(list, dev)) < 0)
        return NULL;

    return list->devs[idx];
}


ssize_t
virNVMeDeviceListLookupIndex(virNVMeDeviceListPtr list,
                             const virNVMeDevice *dev)
{
    size_t i;

    if (!list)
        return -1;

    for (i = 0; i < list->count; i++) {
        virNVMeDevicePtr other = list->devs[i];

        if (virPCIDeviceAddressEqual(&dev->address, &other->address) &&
            dev->namespace == other->namespace)
            return i;
    }

    return -1;
}


static virNVMeDevicePtr
virNVMeDeviceListLookupByPCIAddress(virNVMeDeviceListPtr list,
                                    const virPCIDeviceAddress *address)
{
    size_t i;

    if (!list)
        return NULL;

    for (i = 0; i < list->count; i++) {
        virNVMeDevicePtr other = list->devs[i];

        if (virPCIDeviceAddressEqual(address, &other->address))
            return other;
    }

    return NULL;
}


virPCIDeviceListPtr
virNVMeDeviceListCreateDetachList(virNVMeDeviceListPtr activeList,
                                  virNVMeDeviceListPtr toDetachList)
{
    VIR_AUTOUNREF(virPCIDeviceListPtr) pciDevices = NULL;
    size_t i;

    if (!(pciDevices = virPCIDeviceListNew()))
        return NULL;

    for (i = 0; i < toDetachList->count; i++) {
        const virNVMeDevice *d = toDetachList->devs[i];
        VIR_AUTOPTR(virPCIDevice) pci = NULL;

        /* If there is a NVMe device with the same PCI address on
         * the activeList, the device is already detached. */
        if (virNVMeDeviceListLookupByPCIAddress(activeList, &d->address))
            continue;

        /* It may happen that we want to detach two namespaces
         * from the same NVMe device. This will be represented as
         * two different instances of virNVMeDevice, but
         * obviously we want to put the PCI device on the detach
         * list only once. */
        if (virPCIDeviceListFindByIDs(pciDevices,
                                      d->address.domain,
                                      d->address.bus,
                                      d->address.slot,
                                      d->address.function))
            continue;

        if (!(pci = virPCIDeviceNew(d->address.domain,
                                    d->address.bus,
                                    d->address.slot,
                                    d->address.function)))
            return NULL;

        /* NVMe devices must be bound to vfio */
        virPCIDeviceSetStubDriver(pci, VIR_PCI_STUB_DRIVER_VFIO);
        virPCIDeviceSetManaged(pci, d->managed);

        if (virPCIDeviceListAdd(pciDevices, pci) < 0)
            return NULL;

        /* avoid freeing the device */
        pci = NULL;
    }

    VIR_RETURN_PTR(pciDevices);
}


virPCIDeviceListPtr
virNVMeDeviceListCreateReAttachList(virNVMeDeviceListPtr activeList,
                                    virNVMeDeviceListPtr toReAttachList)
{
    VIR_AUTOUNREF(virPCIDeviceListPtr) pciDevices = NULL;
    size_t i;

    if (!(pciDevices = virPCIDeviceListNew()))
        return NULL;

    for (i = 0; i < toReAttachList->count; i++) {
        const virNVMeDevice *d = toReAttachList->devs[i];
        VIR_AUTOPTR(virPCIDevice) pci = NULL;
        size_t nused = 0;

        /* Check if there is any other NVMe device with the same PCI address as
         * @d. To simplify this, let's just count how many NVMe devices with
         * the same PCI address there are on the @activeList. */
        for (i = 0; i < activeList->count; i++) {
            virNVMeDevicePtr other = activeList->devs[i];

            if (!virPCIDeviceAddressEqual(&d->address, &other->address))
                continue;

            nused++;
        }

        /* Now, the following cases can happen:
         * nused > 1  -> there are other NVMe device active, do NOT detach it
         * nused == 1 -> we've found only @d on the @activeList, detach it
         * nused == 0 -> huh, wait, what? @d is NOT on the @active list, how can
         *               we reattach it?
         */

        if (nused == 0) {
            /* Shouldn't happen (TM) */
            VIR_AUTOFREE(char *) addrStr = virPCIDeviceAddressAsString(&d->address);
            virReportError(VIR_ERR_INTERNAL_ERROR,
                           _("NVMe device %s namespace %lu not found"),
                           NULLSTR(addrStr), d->namespace);
            return NULL;
        } else if (nused > 1) {
            /* NVMe device is still in use */
            continue;
        }

        /* nused == 1 -> detach the device */
        if (!(pci = virPCIDeviceNew(d->address.domain,
                                    d->address.bus,
                                    d->address.slot,
                                    d->address.function)))
            return NULL;

        /* NVMe devices must be bound to vfio */
        virPCIDeviceSetStubDriver(pci, VIR_PCI_STUB_DRIVER_VFIO);
        virPCIDeviceSetManaged(pci, d->managed);

        if (virPCIDeviceListAdd(pciDevices, pci) < 0)
            return NULL;

        /* avoid freeing the device */
        pci = NULL;
    }

    VIR_RETURN_PTR(pciDevices);
}
