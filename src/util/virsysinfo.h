/*
 * virsysinfo.h: get SMBIOS/sysinfo information from the host
 *
 * Copyright (C) 2010-2011 Red Hat, Inc.
 * Copyright (C) 2010 Daniel Veillard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virbuffer.h"
#include "virenum.h"

typedef enum {
    VIR_SYSINFO_SMBIOS,
    VIR_SYSINFO_FWCFG,

    VIR_SYSINFO_LAST
} virSysinfoType;

typedef struct _virSysinfoProcessorDef virSysinfoProcessorDef;
typedef virSysinfoProcessorDef *virSysinfoProcessorDefPtr;
struct _virSysinfoProcessorDef {
    char *processor_socket_destination;
    char *processor_type;
    char *processor_family;
    char *processor_manufacturer;
    char *processor_signature;
    char *processor_version;
    char *processor_external_clock;
    char *processor_max_speed;
    char *processor_status;
    char *processor_serial_number;
    char *processor_part_number;
};

typedef struct _virSysinfoMemoryDef virSysinfoMemoryDef;
typedef virSysinfoMemoryDef *virSysinfoMemoryDefPtr;
struct _virSysinfoMemoryDef {
    char *memory_size;
    char *memory_form_factor;
    char *memory_locator;
    char *memory_bank_locator;
    char *memory_type;
    char *memory_type_detail;
    char *memory_speed;
    char *memory_manufacturer;
    char *memory_serial_number;
    char *memory_part_number;
};

typedef struct _virSysinfoBIOSDef virSysinfoBIOSDef;
typedef virSysinfoBIOSDef *virSysinfoBIOSDefPtr;
struct _virSysinfoBIOSDef {
    char *vendor;
    char *version;
    char *date;
    char *release;
};

typedef struct _virSysinfoSystemDef virSysinfoSystemDef;
typedef virSysinfoSystemDef *virSysinfoSystemDefPtr;
struct _virSysinfoSystemDef {
    char *manufacturer;
    char *product;
    char *version;
    char *serial;
    char *uuid;
    char *sku;
    char *family;
};

typedef struct _virSysinfoBaseBoardDef virSysinfoBaseBoardDef;
typedef virSysinfoBaseBoardDef *virSysinfoBaseBoardDefPtr;
struct _virSysinfoBaseBoardDef {
    char *manufacturer;
    char *product;
    char *version;
    char *serial;
    char *asset;
    char *location;
    /* XXX board type */
};

typedef struct _virSysinfoChassisDef virSysinfoChassisDef;
typedef virSysinfoChassisDef *virSysinfoChassisDefPtr;
struct _virSysinfoChassisDef {
    char *manufacturer;
    char *version;
    char *serial;
    char *asset;
    char *sku;
};

typedef struct _virSysinfoOEMStringsDef virSysinfoOEMStringsDef;
typedef virSysinfoOEMStringsDef *virSysinfoOEMStringsDefPtr;
struct _virSysinfoOEMStringsDef {
    size_t nvalues;
    char **values;
};

typedef struct _virSysinfoFWCfgDef virSysinfoFWCfgDef;
typedef virSysinfoFWCfgDef *virSysinfoFWCfgDefPtr;
struct _virSysinfoFWCfgDef {
    char *name;
    char *value;
    char *file;
};

typedef struct _virSysinfoDef virSysinfoDef;
typedef virSysinfoDef *virSysinfoDefPtr;
struct _virSysinfoDef {
    virSysinfoType type;

    /* The following members are valid for type == VIR_SYSINFO_SMBIOS */
    virSysinfoBIOSDefPtr bios;
    virSysinfoSystemDefPtr system;

    size_t nbaseBoard;
    virSysinfoBaseBoardDefPtr baseBoard;

    virSysinfoChassisDefPtr chassis;

    size_t nprocessor;
    virSysinfoProcessorDefPtr processor;

    size_t nmemory;
    virSysinfoMemoryDefPtr memory;

    virSysinfoOEMStringsDefPtr oemStrings;

    /* The following members are valid for type == VIR_SYSINFO_FWCFG */
    size_t nfw_cfgs;
    virSysinfoFWCfgDefPtr fw_cfgs;
};

virSysinfoDefPtr virSysinfoRead(void);

void virSysinfoBIOSDefFree(virSysinfoBIOSDefPtr def);
void virSysinfoSystemDefFree(virSysinfoSystemDefPtr def);
void virSysinfoBaseBoardDefClear(virSysinfoBaseBoardDefPtr def);
void virSysinfoChassisDefFree(virSysinfoChassisDefPtr def);
void virSysinfoOEMStringsDefFree(virSysinfoOEMStringsDefPtr def);
void virSysinfoDefFree(virSysinfoDefPtr def);

G_DEFINE_AUTO_CLEANUP_FREE_FUNC(virSysinfoDefPtr, virSysinfoDefFree, NULL);

int virSysinfoFormat(virBufferPtr buf, virSysinfoDefPtr def)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

bool virSysinfoIsEqual(virSysinfoDefPtr src,
                       virSysinfoDefPtr dst);

VIR_ENUM_DECL(virSysinfo);
