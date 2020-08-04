/*
 * Copyright 2014, diateam (www.diateam.net)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

#define VBOX_UUID_REGEX "([a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12})"

/*Stores VirtualBox xml hard disk information
A hard disk can have a parent and children*/
typedef struct _virVBoxSnapshotConfHardDisk virVBoxSnapshotConfHardDisk;
typedef virVBoxSnapshotConfHardDisk *virVBoxSnapshotConfHardDiskPtr;
struct _virVBoxSnapshotConfHardDisk {
    virVBoxSnapshotConfHardDiskPtr parent;
    char *uuid;
    char *location;
    char *format;
    char *type;
    size_t nchildren;
    virVBoxSnapshotConfHardDiskPtr *children;
};

/*Stores Virtualbox xml media registry information
We separate disks from other media to manipulate them*/
typedef struct _virVBoxSnapshotConfMediaRegistry virVBoxSnapshotConfMediaRegistry;
typedef virVBoxSnapshotConfMediaRegistry *virVBoxSnapshotConfMediaRegistryPtr;
struct _virVBoxSnapshotConfMediaRegistry {
    size_t ndisks;
    virVBoxSnapshotConfHardDiskPtr *disks;
    size_t notherMedia;
    char **otherMedia;
};

/*Stores VirtualBox xml snapshot information
A snapshot can have a parent and children*/
typedef struct _virVBoxSnapshotConfSnapshot virVBoxSnapshotConfSnapshot;
typedef virVBoxSnapshotConfSnapshot *virVBoxSnapshotConfSnapshotPtr;
struct _virVBoxSnapshotConfSnapshot {
    virVBoxSnapshotConfSnapshotPtr parent;
    char *uuid;
    char *name;
    char *timeStamp;
    char *description;
    char *hardware;
    char *storageController;
    size_t nchildren;
    virVBoxSnapshotConfSnapshotPtr *children;
};

/*Stores VirtualBox xml Machine information*/
typedef struct _virVBoxSnapshotConfMachine virVBoxSnapshotConfMachine;
typedef virVBoxSnapshotConfMachine *virVBoxSnapshotConfMachinePtr;
struct _virVBoxSnapshotConfMachine {
    char *uuid;
    char *name;
    char *currentSnapshot;
    char *snapshotFolder;
    int currentStateModified;
    char *lastStateChange;
    virVBoxSnapshotConfMediaRegistryPtr mediaRegistry;
    char *hardware;
    char *extraData;
    virVBoxSnapshotConfSnapshotPtr snapshot;
    char *storageController;
};

void
virVboxSnapshotConfHardDiskFree(virVBoxSnapshotConfHardDiskPtr disk);
void
virVBoxSnapshotConfMediaRegistryFree(virVBoxSnapshotConfMediaRegistryPtr mediaRegistry);
void
virVBoxSnapshotConfSnapshotFree(virVBoxSnapshotConfSnapshotPtr snapshot);
void
virVBoxSnapshotConfMachineFree(virVBoxSnapshotConfMachinePtr machine);

virVBoxSnapshotConfMachinePtr
virVBoxSnapshotConfLoadVboxFile(const char *filePath,
                                const char *machineLocation);
int
virVBoxSnapshotConfAddSnapshotToXmlMachine(virVBoxSnapshotConfSnapshotPtr snapshot,
                                           virVBoxSnapshotConfMachinePtr machine,
                                           const char *snapshotParentName);
int
virVBoxSnapshotConfAddHardDiskToMediaRegistry(virVBoxSnapshotConfHardDiskPtr hardDisk,
                                              virVBoxSnapshotConfMediaRegistryPtr mediaRegistry,
                                              const char *parentHardDiskId);
int
virVBoxSnapshotConfRemoveSnapshot(virVBoxSnapshotConfMachinePtr machine,
                                  const char *snapshotName);
int
virVBoxSnapshotConfRemoveHardDisk(virVBoxSnapshotConfMediaRegistryPtr mediaRegistry,
                                  const char *uuid);
int
virVBoxSnapshotConfSaveVboxFile(virVBoxSnapshotConfMachinePtr machine,
                                const char *filePath);
int
virVBoxSnapshotConfIsCurrentSnapshot(virVBoxSnapshotConfMachinePtr machine,
                                     const char *snapshotName);
int
virVBoxSnapshotConfGetRWDisksPathsFromLibvirtXML(const char *filePath,
                                                 char ***realReadWriteDisksPath);
int
virVBoxSnapshotConfGetRODisksPathsFromLibvirtXML(const char *filePath,
                                                 char ***realReadOnlyDisksPath);
const char *
virVBoxSnapshotConfHardDiskUuidByLocation(virVBoxSnapshotConfMachinePtr machine,
                                          const char *location);
size_t
virVBoxSnapshotConfDiskListToOpen(virVBoxSnapshotConfMachinePtr machine,
                                  virVBoxSnapshotConfHardDiskPtr **hardDiskToOpen,
                                  const char *location);
int
virVBoxSnapshotConfRemoveFakeDisks(virVBoxSnapshotConfMachinePtr machine);
int
virVBoxSnapshotConfDiskIsInMediaRegistry(virVBoxSnapshotConfMachinePtr machine,
                                         const char *location);
virVBoxSnapshotConfHardDiskPtr
virVBoxSnapshotConfHardDiskPtrByLocation(virVBoxSnapshotConfMachinePtr machine,
                                         const char *location);
virVBoxSnapshotConfSnapshotPtr
virVBoxSnapshotConfSnapshotByName(virVBoxSnapshotConfSnapshotPtr snapshot,
                                  const char *snapshotName);
