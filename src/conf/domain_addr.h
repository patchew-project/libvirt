/*
 * domain_addr.h: helper APIs for managing domain device addresses
 *
 * Copyright (C) 2006-2016 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
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
 * Author: Daniel P. Berrange <berrange@redhat.com>
 */

#ifndef __DOMAIN_ADDR_H__
# define __DOMAIN_ADDR_H__

# include "domain_conf.h"

# define VIR_PCI_ADDRESS_SLOT_LAST 31
# define VIR_PCI_ADDRESS_FUNCTION_LAST 7

typedef enum {
   VIR_PCI_CONNECT_HOTPLUGGABLE = 1 << 0, /* is hotplug needed/supported */

   /* set for devices that can share a single slot in auto-assignment
    * (by assigning one device to each of the 8 functions on the slot)
    */
   VIR_PCI_CONNECT_AGGREGATE_SLOT = 1 << 1,

   /* kinds of devices as a bitmap so they can be combined (some PCI
    * controllers permit connecting multiple types of devices)
    */
   VIR_PCI_CONNECT_TYPE_PCI_DEVICE = 1 << 2,
   VIR_PCI_CONNECT_TYPE_PCIE_DEVICE = 1 << 3,
   VIR_PCI_CONNECT_TYPE_PCIE_ROOT_PORT = 1 << 4,
   VIR_PCI_CONNECT_TYPE_PCIE_SWITCH_UPSTREAM_PORT = 1 << 5,
   VIR_PCI_CONNECT_TYPE_PCIE_SWITCH_DOWNSTREAM_PORT = 1 << 6,
   VIR_PCI_CONNECT_TYPE_DMI_TO_PCI_BRIDGE = 1 << 7,
   VIR_PCI_CONNECT_TYPE_PCI_EXPANDER_BUS = 1 << 8,
   VIR_PCI_CONNECT_TYPE_PCIE_EXPANDER_BUS = 1 << 9,
   VIR_PCI_CONNECT_TYPE_PCI_BRIDGE = 1 << 10,
} virDomainPCIConnectFlags;

/* a combination of all bits that describe the type of connections
 * allowed, e.g. PCI, PCIe, switch
 */
# define VIR_PCI_CONNECT_TYPES_MASK \
   (VIR_PCI_CONNECT_TYPE_PCI_DEVICE | VIR_PCI_CONNECT_TYPE_PCIE_DEVICE | \
    VIR_PCI_CONNECT_TYPE_PCIE_SWITCH_UPSTREAM_PORT | \
    VIR_PCI_CONNECT_TYPE_PCIE_SWITCH_DOWNSTREAM_PORT | \
    VIR_PCI_CONNECT_TYPE_PCIE_ROOT_PORT | \
    VIR_PCI_CONNECT_TYPE_DMI_TO_PCI_BRIDGE | \
    VIR_PCI_CONNECT_TYPE_PCI_EXPANDER_BUS | \
    VIR_PCI_CONNECT_TYPE_PCIE_EXPANDER_BUS | \
    VIR_PCI_CONNECT_TYPE_PCI_BRIDGE)

/* combination of all bits that could be used to connect a normal
 * endpoint device (i.e. excluding the connection possible between an
 * upstream and downstream switch port, or a PCIe root port and a PCIe
 * port)
 */
# define VIR_PCI_CONNECT_TYPES_ENDPOINT \
   (VIR_PCI_CONNECT_TYPE_PCI_DEVICE | VIR_PCI_CONNECT_TYPE_PCIE_DEVICE)

virDomainPCIConnectFlags
virDomainPCIControllerModelToConnectType(virDomainControllerModelPCI model);

typedef struct {
    /* each function is represented by one bit, set if that function is
     * in use by a device, or clear if it isn't.
     */
    uint8_t functions;

    /* aggregate is true if this slot has only devices with
     * VIR_PCI_CONNECT_AGGREGATE assigned to its functions (meaning
     * that other devices with the same flags could also be
     * auto-assigned to the other functions)
     */
    bool aggregate;
} virDomainPCIAddressSlot;

typedef struct {
    virDomainControllerModelPCI model;
    /* flags and min/max can be computed from model, but
     * having them ready makes life easier.
     */
    virDomainPCIConnectFlags flags;
    size_t minSlot, maxSlot; /* usually 0,0 or 0,31, or 1,31 */
    /* Each bit in a slot represents one function on that slot. If the
     * bit is set, that function is in use by a device.
     */
    virDomainPCIAddressSlot slot[VIR_PCI_ADDRESS_SLOT_LAST + 1];
} virDomainPCIAddressBus;
typedef virDomainPCIAddressBus *virDomainPCIAddressBusPtr;

struct _virDomainPCIAddressSet {
    virDomainPCIAddressBus *buses;
    size_t nbuses;
    virPCIDeviceAddress lastaddr;
    virDomainPCIConnectFlags lastFlags;
    bool dryRun;          /* on a dry run, new buses are auto-added
                             and addresses aren't saved in device infos */
};
typedef struct _virDomainPCIAddressSet virDomainPCIAddressSet;
typedef virDomainPCIAddressSet *virDomainPCIAddressSetPtr;

char *virDomainPCIAddressAsString(virPCIDeviceAddressPtr addr);

virDomainPCIAddressSetPtr virDomainPCIAddressSetAlloc(unsigned int nbuses);

void virDomainPCIAddressSetFree(virDomainPCIAddressSetPtr addrs);

bool virDomainPCIAddressFlagsCompatible(virPCIDeviceAddressPtr addr,
                                        const char *addrStr,
                                        virDomainPCIConnectFlags busFlags,
                                        virDomainPCIConnectFlags devFlags,
                                        bool reportError,
                                        bool fromConfig);

bool virDomainPCIAddressValidate(virDomainPCIAddressSetPtr addrs,
                                 virPCIDeviceAddressPtr addr,
                                 const char *addrStr,
                                 virDomainPCIConnectFlags flags,
                                 bool fromConfig);


int virDomainPCIAddressBusSetModel(virDomainPCIAddressBusPtr bus,
                                   virDomainControllerModelPCI model);

bool virDomainPCIAddressSlotInUse(virDomainPCIAddressSetPtr addrs,
                                  virPCIDeviceAddressPtr addr);

int virDomainPCIAddressSetGrow(virDomainPCIAddressSetPtr addrs,
                               virPCIDeviceAddressPtr addr,
                               virDomainPCIConnectFlags flags);

int virDomainPCIAddressReserveAddr(virDomainPCIAddressSetPtr addrs,
                                   virPCIDeviceAddressPtr addr,
                                   virDomainPCIConnectFlags flags);

int virDomainPCIAddressEnsureAddr(virDomainPCIAddressSetPtr addrs,
                                  virDomainDeviceInfoPtr dev,
                                  virDomainPCIConnectFlags flags);

int virDomainPCIAddressReleaseAddr(virDomainPCIAddressSetPtr addrs,
                                   virPCIDeviceAddressPtr addr);

int virDomainPCIAddressReserveNextAddr(virDomainPCIAddressSetPtr addrs,
                                       virDomainDeviceInfoPtr dev,
                                       virDomainPCIConnectFlags flags,
                                       int function);

void virDomainPCIAddressSetAllMulti(virDomainDefPtr def);

struct _virDomainCCWAddressSet {
    virHashTablePtr defined;
    virDomainDeviceCCWAddress next;
};
typedef struct _virDomainCCWAddressSet virDomainCCWAddressSet;
typedef virDomainCCWAddressSet *virDomainCCWAddressSetPtr;

int virDomainCCWAddressAssign(virDomainDeviceInfoPtr dev,
                              virDomainCCWAddressSetPtr addrs,
                              bool autoassign);
void virDomainCCWAddressSetFree(virDomainCCWAddressSetPtr addrs);
int virDomainCCWAddressAllocate(virDomainDefPtr def,
                                virDomainDeviceDefPtr dev,
                                virDomainDeviceInfoPtr info,
                                void *data);
int virDomainCCWAddressValidate(virDomainDefPtr def,
                                virDomainDeviceDefPtr dev,
                                virDomainDeviceInfoPtr info,
                                void *data);

int virDomainCCWAddressReleaseAddr(virDomainCCWAddressSetPtr addrs,
                                   virDomainDeviceInfoPtr dev);
virDomainCCWAddressSetPtr virDomainCCWAddressSetCreate(void);

struct _virDomainVirtioSerialController {
    unsigned int idx;
    virBitmapPtr ports;
};

typedef struct _virDomainVirtioSerialController virDomainVirtioSerialController;
typedef virDomainVirtioSerialController *virDomainVirtioSerialControllerPtr;

struct _virDomainVirtioSerialAddrSet {
    virDomainVirtioSerialControllerPtr *controllers;
    size_t ncontrollers;
};
typedef struct _virDomainVirtioSerialAddrSet virDomainVirtioSerialAddrSet;
typedef virDomainVirtioSerialAddrSet *virDomainVirtioSerialAddrSetPtr;

virDomainVirtioSerialAddrSetPtr
virDomainVirtioSerialAddrSetCreate(void);
int
virDomainVirtioSerialAddrSetAddControllers(virDomainVirtioSerialAddrSetPtr addrs,
                                           virDomainDefPtr def);
void
virDomainVirtioSerialAddrSetFree(virDomainVirtioSerialAddrSetPtr addrs);
virDomainVirtioSerialAddrSetPtr
virDomainVirtioSerialAddrSetCreateFromDomain(virDomainDefPtr def);
bool
virDomainVirtioSerialAddrIsComplete(virDomainDeviceInfoPtr info);
int
virDomainVirtioSerialAddrAutoAssignFromCache(virDomainDefPtr def,
                                             virDomainVirtioSerialAddrSetPtr addrs,
                                             virDomainDeviceInfoPtr info,
                                             bool allowZero);

int
virDomainVirtioSerialAddrAutoAssign(virDomainDefPtr def,
                                    virDomainDeviceInfoPtr info,
                                    bool allowZero);

int
virDomainVirtioSerialAddrAssign(virDomainDefPtr def,
                                virDomainVirtioSerialAddrSetPtr addrs,
                                virDomainDeviceInfoPtr info,
                                bool allowZero,
                                bool portOnly);

int
virDomainVirtioSerialAddrReserve(virDomainDefPtr def,
                                 virDomainDeviceDefPtr dev,
                                 virDomainDeviceInfoPtr info,
                                 void *data);

int
virDomainVirtioSerialAddrRelease(virDomainVirtioSerialAddrSetPtr addrs,
                                 virDomainDeviceInfoPtr info);

bool
virDomainUSBAddressPortIsValid(unsigned int *port);

void
virDomainUSBAddressPortFormatBuf(virBufferPtr buf,
                                 unsigned int *port);
char *
virDomainUSBAddressPortFormat(unsigned int *port);

# define VIR_DOMAIN_USB_HUB_PORTS 8

typedef struct _virDomainUSBAddressHub virDomainUSBAddressHub;
typedef virDomainUSBAddressHub *virDomainUSBAddressHubPtr;
struct _virDomainUSBAddressHub {
    /* indexes are shifted by one:
     * ports[0] represents port 1, because ports are numbered from 1 */
    virBitmapPtr portmap;
    size_t nports;
    virDomainUSBAddressHubPtr *ports;
};

struct _virDomainUSBAddressSet {
    /* every <controller type='usb' index='i'> is represented
     * as a hub at buses[i] */
    virDomainUSBAddressHubPtr *buses;
    size_t nbuses;
};
typedef struct _virDomainUSBAddressSet virDomainUSBAddressSet;
typedef virDomainUSBAddressSet *virDomainUSBAddressSetPtr;

virDomainUSBAddressSetPtr virDomainUSBAddressSetCreate(void);

int virDomainUSBAddressSetAddControllers(virDomainUSBAddressSetPtr addrs,
                                         virDomainDefPtr def);
int
virDomainUSBAddressSetAddHub(virDomainUSBAddressSetPtr addrs,
                             virDomainHubDefPtr hub);
size_t
virDomainUSBAddressCountAllPorts(virDomainDefPtr def);
void virDomainUSBAddressSetFree(virDomainUSBAddressSetPtr addrs);

int
virDomainUSBAddressPresent(virDomainDeviceInfoPtr info,
                           void *data);
int
virDomainUSBAddressReserve(virDomainDeviceInfoPtr info,
                           void *data);

int
virDomainUSBAddressAssign(virDomainUSBAddressSetPtr addrs,
                          virDomainDeviceInfoPtr info);

int
virDomainUSBAddressEnsure(virDomainUSBAddressSetPtr addrs,
                          virDomainDeviceInfoPtr info);

int
virDomainUSBAddressRelease(virDomainUSBAddressSetPtr addrs,
                           virDomainDeviceInfoPtr info);
#endif /* __DOMAIN_ADDR_H__ */
