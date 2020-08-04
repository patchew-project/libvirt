/*
 * virnetworkportdef.h: network port XML processing
 *
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "viruuid.h"
#include "virnetdevvlan.h"
#include "virnetdevvportprofile.h"
#include "virnetdevbandwidth.h"
#include "virpci.h"
#include "virxml.h"

typedef struct _virNetworkPortDef virNetworkPortDef;
typedef virNetworkPortDef *virNetworkPortDefPtr;

typedef enum {
    VIR_NETWORK_PORT_PLUG_TYPE_NONE,
    VIR_NETWORK_PORT_PLUG_TYPE_NETWORK,
    VIR_NETWORK_PORT_PLUG_TYPE_BRIDGE,
    VIR_NETWORK_PORT_PLUG_TYPE_DIRECT,
    VIR_NETWORK_PORT_PLUG_TYPE_HOSTDEV_PCI,

    VIR_NETWORK_PORT_PLUG_TYPE_LAST,
} virNetworkPortPlugType;

VIR_ENUM_DECL(virNetworkPortPlug);

struct _virNetworkPortDef {
    unsigned char uuid[VIR_UUID_BUFLEN];
    char *ownername;
    unsigned char owneruuid[VIR_UUID_BUFLEN];

    char *group;
    virMacAddr mac;

    virNetDevVPortProfilePtr virtPortProfile;
    virNetDevBandwidthPtr bandwidth;
    unsigned int class_id; /* class ID for bandwidth 'floor' */
    virNetDevVlan vlan;
    int trustGuestRxFilters; /* enum virTristateBool */
    virTristateBool isolatedPort;

    int plugtype; /* virNetworkPortPlugType */
    union {
        struct {
            char *brname;
            int macTableManager; /* enum virNetworkBridgeMACTableManagerType */
        } bridge; /* For TYPE_NETWORK & TYPE_BRIDGE */
        struct {
            char *linkdev;
            int mode; /* enum virNetDevMacVLanMode from util/virnetdevmacvlan.h */
        } direct;
        struct {
            virPCIDeviceAddress addr; /* PCI Address of device */
            int driver; /* virNetworkForwardDriverNameType */
            int managed;
        } hostdevpci;
    } plug;
};


void
virNetworkPortDefFree(virNetworkPortDefPtr port);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(virNetworkPortDef, virNetworkPortDefFree);

virNetworkPortDefPtr
virNetworkPortDefParseNode(xmlDocPtr xml,
                           xmlNodePtr root);

virNetworkPortDefPtr
virNetworkPortDefParseString(const char *xml);

virNetworkPortDefPtr
virNetworkPortDefParseFile(const char *filename);

char *
virNetworkPortDefFormat(const virNetworkPortDef *def);

int
virNetworkPortDefFormatBuf(virBufferPtr buf,
                           const virNetworkPortDef *def);

int
virNetworkPortDefSaveStatus(virNetworkPortDef *def,
                            const char *dir);

int
virNetworkPortDefDeleteStatus(virNetworkPortDef *def,
                              const char *dir);
