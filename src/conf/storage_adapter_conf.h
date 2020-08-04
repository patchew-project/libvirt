/*
 * storage_adapter_conf.h: helpers to handle storage pool adapter manipulation
 *                         (derived from storage_conf.h)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virpci.h"
#include "virxml.h"
#include "virenum.h"


typedef enum {
    VIR_STORAGE_ADAPTER_TYPE_DEFAULT = 0,
    VIR_STORAGE_ADAPTER_TYPE_SCSI_HOST,
    VIR_STORAGE_ADAPTER_TYPE_FC_HOST,

    VIR_STORAGE_ADAPTER_TYPE_LAST,
} virStorageAdapterType;
VIR_ENUM_DECL(virStorageAdapter);

typedef struct _virStorageAdapterSCSIHost virStorageAdapterSCSIHost;
typedef virStorageAdapterSCSIHost *virStorageAdapterSCSIHostPtr;
struct _virStorageAdapterSCSIHost {
    char *name;
    virPCIDeviceAddress parentaddr; /* host address */
    int unique_id;
    bool has_parent;
};

typedef struct _virStorageAdapterFCHost virStorageAdapterFCHost;
typedef virStorageAdapterFCHost *virStorageAdapterFCHostPtr;
struct _virStorageAdapterFCHost {
    char *parent;
    char *parent_wwnn;
    char *parent_wwpn;
    char *parent_fabric_wwn;
    char *wwnn;
    char *wwpn;
    int managed;        /* enum virTristateSwitch */
};

typedef struct _virStorageAdapter virStorageAdapter;
typedef virStorageAdapter *virStorageAdapterPtr;
struct _virStorageAdapter {
    int type; /* virStorageAdapterType */

    union {
        virStorageAdapterSCSIHost scsi_host;
        virStorageAdapterFCHost fchost;
    } data;
};


void
virStorageAdapterClear(virStorageAdapterPtr adapter);

int
virStorageAdapterParseXML(virStorageAdapterPtr adapter,
                          xmlNodePtr node,
                          xmlXPathContextPtr ctxt);

int
virStorageAdapterValidate(virStorageAdapterPtr adapter);

void
virStorageAdapterFormat(virBufferPtr buf,
                        virStorageAdapterPtr adapter);
