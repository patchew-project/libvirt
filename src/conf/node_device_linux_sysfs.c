/*
 * node_device_linux_sysfs.c: Linux specific code to gather device data
 * that is available from sysfs (but not from UDEV or HAL).
 *
 * Copyright (C) 2009-2015 Red Hat, Inc.
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
 */

#include <config.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>

#include "node_device_linux_sysfs.h"
#include "virerror.h"
#include "viralloc.h"
#include "virlog.h"
#include "virfile.h"
#include "virscsihost.h"
#include "virstring.h"
#include "virvhba.h"

#define VIR_FROM_THIS VIR_FROM_NODEDEV

#ifdef __linux__

VIR_LOG_INIT("node_device.node_device_linux_sysfs");

int
virNodeDeviceSysfsGetSCSIHostCaps(virNodeDevCapSCSIHostPtr scsi_host)
{
    char *tmp = NULL;
    int ret = -1;

    if ((scsi_host->unique_id =
         virSCSIHostGetUniqueId(NULL, scsi_host->host)) < 0) {
        VIR_DEBUG("Failed to read unique_id for host%d", scsi_host->host);
        scsi_host->unique_id = -1;
    }

    VIR_DEBUG("Checking if host%d is an FC HBA", scsi_host->host);

    if (virVHBAPathExists(NULL, scsi_host->host)) {
        scsi_host->flags |= VIR_NODE_DEV_CAP_FLAG_HBA_FC_HOST;

        if (!(tmp = virVHBAGetConfig(NULL, scsi_host->host, "port_name"))) {
            VIR_WARN("Failed to read WWPN for host%d", scsi_host->host);
            goto cleanup;
        }
        VIR_FREE(scsi_host->wwpn);
        VIR_STEAL_PTR(scsi_host->wwpn, tmp);

        if (!(tmp = virVHBAGetConfig(NULL, scsi_host->host, "node_name"))) {
            VIR_WARN("Failed to read WWNN for host%d", scsi_host->host);
            goto cleanup;
        }
        VIR_FREE(scsi_host->wwnn);
        VIR_STEAL_PTR(scsi_host->wwnn, tmp);

        if ((tmp = virVHBAGetConfig(NULL, scsi_host->host, "fabric_name"))) {
            VIR_FREE(scsi_host->fabric_wwn);
            VIR_STEAL_PTR(scsi_host->fabric_wwn, tmp);
        }
    }

    if (virVHBAIsVportCapable(NULL, scsi_host->host)) {
        scsi_host->flags |= VIR_NODE_DEV_CAP_FLAG_HBA_VPORT_OPS;

        if (!(tmp = virVHBAGetConfig(NULL, scsi_host->host,
                                     "max_npiv_vports"))) {
            VIR_WARN("Failed to read max_npiv_vports for host%d",
                     scsi_host->host);
            goto cleanup;
        }

        if (virStrToLong_i(tmp, NULL, 10, &scsi_host->max_vports) < 0) {
            VIR_WARN("Failed to parse value of max_npiv_vports '%s'", tmp);
            goto cleanup;
        }

         if (!(tmp = virVHBAGetConfig(NULL, scsi_host->host,
                                      "npiv_vports_inuse"))) {
            VIR_WARN("Failed to read npiv_vports_inuse for host%d",
                     scsi_host->host);
            goto cleanup;
        }

        if (virStrToLong_i(tmp, NULL, 10, &scsi_host->vports) < 0) {
            VIR_WARN("Failed to parse value of npiv_vports_inuse '%s'", tmp);
            goto cleanup;
        }
    }

    ret = 0;
 cleanup:
    if (ret < 0) {
        /* Clear the two flags in case of producing confusing XML output */
        scsi_host->flags &= ~(VIR_NODE_DEV_CAP_FLAG_HBA_FC_HOST |
                                VIR_NODE_DEV_CAP_FLAG_HBA_VPORT_OPS);

        VIR_FREE(scsi_host->wwnn);
        VIR_FREE(scsi_host->wwpn);
        VIR_FREE(scsi_host->fabric_wwn);
    }
    VIR_FREE(tmp);
    return ret;
}


static int
virNodeDeviceSysfsGetPCISRIOVCaps(const char *sysfsPath,
                                  virNodeDevCapPCIDevPtr pci_dev)
{
    size_t i;
    int ret;

    /* this could be a refresh, so clear out the old data */
    for (i = 0; i < pci_dev->num_virtual_functions; i++)
       VIR_FREE(pci_dev->virtual_functions[i]);
    VIR_FREE(pci_dev->virtual_functions);
    pci_dev->num_virtual_functions = 0;
    pci_dev->max_virtual_functions = 0;
    pci_dev->flags &= ~VIR_NODE_DEV_CAP_FLAG_PCI_VIRTUAL_FUNCTION;
    pci_dev->flags &= ~VIR_NODE_DEV_CAP_FLAG_PCI_PHYSICAL_FUNCTION;

    ret = virPCIGetPhysicalFunction(sysfsPath,
                                    &pci_dev->physical_function);
    if (ret < 0)
        goto cleanup;

    if (pci_dev->physical_function)
        pci_dev->flags |= VIR_NODE_DEV_CAP_FLAG_PCI_PHYSICAL_FUNCTION;

    ret = virPCIGetVirtualFunctions(sysfsPath, &pci_dev->virtual_functions,
                                    &pci_dev->num_virtual_functions,
                                    &pci_dev->max_virtual_functions);
    if (ret < 0)
        goto cleanup;

    if (pci_dev->num_virtual_functions > 0 ||
        pci_dev->max_virtual_functions > 0)
        pci_dev->flags |= VIR_NODE_DEV_CAP_FLAG_PCI_VIRTUAL_FUNCTION;

 cleanup:
    return ret;
}


static int
virNodeDeviceSysfsGetPCIIOMMUGroupCaps(virNodeDevCapPCIDevPtr pci_dev)
{
    size_t i;
    int tmpGroup, ret = -1;
    virPCIDeviceAddress addr;

    /* this could be a refresh, so clear out the old data */
    for (i = 0; i < pci_dev->nIommuGroupDevices; i++)
       VIR_FREE(pci_dev->iommuGroupDevices[i]);
    VIR_FREE(pci_dev->iommuGroupDevices);
    pci_dev->nIommuGroupDevices = 0;
    pci_dev->iommuGroupNumber = 0;

    addr.domain = pci_dev->domain;
    addr.bus = pci_dev->bus;
    addr.slot = pci_dev->slot;
    addr.function = pci_dev->function;
    tmpGroup = virPCIDeviceAddressGetIOMMUGroupNum(&addr);
    if (tmpGroup == -1) {
        /* error was already reported */
        goto cleanup;
    }
    if (tmpGroup == -2) {
        /* -2 return means there is no iommu_group data */
        ret = 0;
        goto cleanup;
    }
    if (tmpGroup >= 0) {
        if (virPCIDeviceAddressGetIOMMUGroupAddresses(&addr, &pci_dev->iommuGroupDevices,
                                                      &pci_dev->nIommuGroupDevices) < 0)
            goto cleanup;
        pci_dev->iommuGroupNumber = tmpGroup;
    }

    ret = 0;
 cleanup:
    return ret;
}


/* virNodeDeviceSysfsGetPCIRelatedCaps() get info that is stored in sysfs
 * about devices related to this device, i.e. things that can change
 * without this device itself changing. These must be refreshed
 * anytime full XML of the device is requested, because they can
 * change with no corresponding notification from the kernel/udev.
 */
int
virNodeDeviceSysfsGetPCIRelatedDevCaps(const char *sysfsPath,
                                       virNodeDevCapPCIDevPtr pci_dev)
{
    if (virNodeDeviceSysfsGetPCISRIOVCaps(sysfsPath, pci_dev) < 0)
        return -1;
    if (virNodeDeviceSysfsGetPCIIOMMUGroupCaps(pci_dev) < 0)
        return -1;
    return 0;
}


#else

int
virNodeDeviceSysfsGetSCSIHostCaps(virNodeDevCapSCSIHostPtr scsi_host ATTRIBUTE_UNUSED)
{
    return -1;
}

int
virNodeDeviceSysfsGetPCIRelatedDevCaps(const char *sysfsPath ATTRIBUTE_UNUSED,
                                    virNodeDevCapPCIDevPtr pci_dev ATTRIBUTE_UNUSED)
{
    return -1;
}

#endif /* __linux__ */
