/*
 * hyperv_wmi_classes.h: WMI classes for managing Microsoft Hyper-V hosts
 *
 * Copyright (C) 2017 Datto Inc
 * Copyright (C) 2011 Matthias Bolte <matthias.bolte@googlemail.com>
 * Copyright (C) 2009 Michael Sievers <msievers83@googlemail.com>
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

#pragma once

#include <wsman-api.h>

#include "internal.h"

#include "hyperv_wmi_classes.generated.typedef"

#define ROOT_CIMV2 \
    "http://schemas.microsoft.com/wbem/wsman/1/wmi/root/cimv2/*"

#define ROOT_VIRTUALIZATION_V2 \
    "http://schemas.microsoft.com/wbem/wsman/1/wmi/root/virtualization/v2/*"



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Msvm_ComputerSystem
 */

#define MSVM_COMPUTERSYSTEM_WQL_VIRTUAL \
    "Name != __SERVER "

#define MSVM_COMPUTERSYSTEM_WQL_PHYSICAL \
    "Name = __SERVER "

#define MSVM_COMPUTERSYSTEM_WQL_ACTIVE \
    "(EnabledState != 0 and EnabledState != 3 and EnabledState != 32769) "

#define MSVM_COMPUTERSYSTEM_WQL_INACTIVE \
    "(EnabledState = 0 or EnabledState = 3 or EnabledState = 32769) "

enum _Msvm_ComputerSystem_EnabledState {
    MSVM_COMPUTERSYSTEM_ENABLEDSTATE_UNKNOWN = 0,          /* inactive */
    MSVM_COMPUTERSYSTEM_ENABLEDSTATE_ENABLED = 2,          /*   active */
    MSVM_COMPUTERSYSTEM_ENABLEDSTATE_DISABLED = 3,         /* inactive */
    MSVM_COMPUTERSYSTEM_ENABLEDSTATE_PAUSED = 32768,       /*   active */
    MSVM_COMPUTERSYSTEM_ENABLEDSTATE_SUSPENDED = 32769,    /* inactive */
    MSVM_COMPUTERSYSTEM_ENABLEDSTATE_STARTING = 32770,     /*   active */
    MSVM_COMPUTERSYSTEM_ENABLEDSTATE_SNAPSHOTTING = 32771, /*   active */
    MSVM_COMPUTERSYSTEM_ENABLEDSTATE_SAVING = 32773,       /*   active */
    MSVM_COMPUTERSYSTEM_ENABLEDSTATE_STOPPING = 32774,     /*   active */
    MSVM_COMPUTERSYSTEM_ENABLEDSTATE_PAUSING = 32776,      /*   active */
    MSVM_COMPUTERSYSTEM_ENABLEDSTATE_RESUMING = 32777      /*   active */
};

enum _Msvm_ComputerSystem_RequestedState {
    MSVM_COMPUTERSYSTEM_REQUESTEDSTATE_ENABLED = 2,
    MSVM_COMPUTERSYSTEM_REQUESTEDSTATE_DISABLED = 3,
    MSVM_COMPUTERSYSTEM_REQUESTEDSTATE_OFFLINE = 6,
    MSVM_COMPUTERSYSTEM_REQUESTEDSTATE_QUIESCE = 9,
    MSVM_COMPUTERSYSTEM_REQUESTEDSTATE_REBOOT = 10,
    MSVM_COMPUTERSYSTEM_REQUESTEDSTATE_RESET = 11,
    MSVM_COMPUTERSYSTEM_REQUESTEDSTATE_PAUSED = 32768,
    MSVM_COMPUTERSYSTEM_REQUESTEDSTATE_SUSPENDED = 32769,
};



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Msvm_ConcreteJob
 */

enum _Msvm_ConcreteJob_JobState {
    MSVM_CONCRETEJOB_JOBSTATE_NEW = 2,
    MSVM_CONCRETEJOB_JOBSTATE_STARTING = 3,
    MSVM_CONCRETEJOB_JOBSTATE_RUNNING = 4,
    MSVM_CONCRETEJOB_JOBSTATE_SUSPENDED = 5,
    MSVM_CONCRETEJOB_JOBSTATE_SHUTTING_DOWN = 6,
    MSVM_CONCRETEJOB_JOBSTATE_COMPLETED = 7,
    MSVM_CONCRETEJOB_JOBSTATE_TERMINATED = 8,
    MSVM_CONCRETEJOB_JOBSTATE_KILLED = 9,
    MSVM_CONCRETEJOB_JOBSTATE_EXCEPTION = 10,
    MSVM_CONCRETEJOB_JOBSTATE_SERVICE = 11,
};



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Msvm_ResourceAllocationSettingData
 */

/* https://docs.microsoft.com/en-us/windows/win32/hyperv_v2/msvm-resourceallocationsettingdata */
enum _Msvm_ResourceAllocationSettingData_ResourceType {
    MSVM_RASD_RESOURCETYPE_OTHER = 1,
    MSVM_RASD_RESOURCETYPE_IDE_CONTROLLER = 5,
    MSVM_RASD_RESOURCETYPE_PARALLEL_SCSI_HBA = 6,
    MSVM_RASD_RESOURCETYPE_ETHERNET_ADAPTER = 10,
    MSVM_RASD_RESOURCETYPE_DISKETTE_DRIVE = 14,
    MSVM_RASD_RESOURCETYPE_CD_DRIVE = 15,
    MSVM_RASD_RESOURCETYPE_DVD_DRIVE = 16,
    MSVM_RASD_RESOURCETYPE_DISK_DRIVE = 17,
    MSVM_RASD_RESOURCETYPE_STORAGE_EXTENT = 19,
    MSVM_RASD_RESOURCETYPE_SERIAL_PORT = 21,
    MSVM_RASD_RESOURCETYPE_LOGICAL_DISK = 31,
    MSVM_RASD_RESOURCETYPE_ETHERNET_CONNECTION = 33,
};



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Msvm_EthernetPortAllocationSettingData
 */

/* https://docs.microsoft.com/en-us/windows/win32/hyperv_v2/msvm-ethernetportallocationsettingdata#enabled */
enum _Msvm_EthernetPortAllocationSettingData_EnabledState {
    MSVM_ETHERNETPORTALLOCATIONSETTINGDATA_ENABLEDSTATE_ENABLED = 2,
    MSVM_ETHERNETPORTALLOCATIONSETTINGDATA_ENABLEDSTATE_DISABLED = 3,
};



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * CIM_EnabledLogicalElement
 */

/* https://docs.microsoft.com/en-us/windows/win32/hyperv_v2/cim-enabledlogicalelement#Unknown */
enum _CIM_EnabledLogicalElement_EnabledState {
    CIM_ENABLEDLOGICALELEMENT_ENABLEDSTATE_UNKNOWN = 0,
    CIM_ENABLEDLOGICALELEMENT_ENABLEDSTATE_OTHER = 1,
    CIM_ENABLEDLOGICALELEMENT_ENABLEDSTATE_ENABLED = 2,
    CIM_ENABLEDLOGICALELEMENT_ENABLEDSTATE_DISABLED = 3,
    CIM_ENABLEDLOGICALELEMENT_ENABLEDSTATE_SHUTTING_DOWN = 4,
    CIM_ENABLEDLOGICALELEMENT_ENABLEDSTATE_NOT_APPLICABLE = 5,
    CIM_ENABLEDLOGICALELEMENT_ENABLEDSTATE_ENABLED_BUT_OFFLINE = 6,
    CIM_ENABLEDLOGICALELEMENT_ENABLEDSTATE_IN_TEST = 7,
    CIM_ENABLEDLOGICALELEMENT_ENABLEDSTATE_DEFERRED = 8,
    CIM_ENABLEDLOGICALELEMENT_ENABLEDSTATE_QUIESCE = 9,
    CIM_ENABLEDLOGICALELEMENT_ENABLEDSTATE_STARTING = 10,
};



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * WMI
 */

typedef struct _hypervCimType hypervCimType;
typedef hypervCimType *hypervCimTypePtr;
struct _hypervCimType {
    /* Parameter name */
    const char *name;
    /* Parameter type */
    const char *type;
    /* whether parameter is an array type */
    bool isArray;
};

typedef struct _hypervWmiClassInfo hypervWmiClassInfo;
typedef hypervWmiClassInfo *hypervWmiClassInfoPtr;
struct _hypervWmiClassInfo {
    /* The WMI class name */
    const char *name;
    /* The URI for wsman enumerate request */
    const char *rootUri;
    /* The namespace URI for XML serialization */
    const char *resourceUri;
    /* The wsman serializer info - one of the *_TypeInfo structs */
    XmlSerializerInfo *serializerInfo;
    /* Property type information */
    hypervCimTypePtr propertyInfo;
};

#include "hyperv_wmi_classes.generated.h"
