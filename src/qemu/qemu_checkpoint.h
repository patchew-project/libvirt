/*
 * qemu_checkpoint.h: Implementation and handling of checkpoint
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virconftypes.h"
#include "datatypes.h"
#include "qemu_conf.h"

virDomainObjPtr
qemuDomObjFromCheckpoint(virDomainCheckpointPtr checkpoint);

virDomainMomentObjPtr
qemuCheckpointObjFromCheckpoint(virDomainObjPtr vm,
                                virDomainCheckpointPtr checkpoint);

virDomainMomentObjPtr
qemuCheckpointObjFromName(virDomainObjPtr vm,
                          const char *name);

int
qemuCheckpointDiscardAllMetadata(virQEMUDriverPtr driver,
                                 virDomainObjPtr vm);

virDomainCheckpointPtr
qemuCheckpointCreateXML(virDomainPtr domain,
                        virDomainObjPtr vm,
                        const char *xmlDesc,
                        unsigned int flags);


char *
qemuCheckpointGetXMLDesc(virDomainObjPtr vm,
                         virDomainCheckpointPtr checkpoint,
                         unsigned int flags);

int
qemuCheckpointDelete(virDomainObjPtr vm,
                     virDomainCheckpointPtr checkpoint,
                     unsigned int flags);

int
qemuCheckpointCreateCommon(virQEMUDriverPtr driver,
                           virDomainObjPtr vm,
                           virDomainCheckpointDefPtr *def,
                           virJSONValuePtr *actions,
                           virDomainMomentObjPtr *chk);

int
qemuCheckpointCreateFinalize(virQEMUDriverPtr driver,
                             virDomainObjPtr vm,
                             virQEMUDriverConfigPtr cfg,
                             virDomainMomentObjPtr chk,
                             bool update_current);

void
qemuCheckpointRollbackMetadata(virDomainObjPtr vm,
                               virDomainMomentObjPtr chk);

int
qemuCheckpointDiscardDiskBitmaps(virStorageSourcePtr src,
                                 virHashTablePtr blockNamedNodeData,
                                 const char *delbitmap,
                                 virJSONValuePtr actions,
                                 const char *diskdst,
                                 GSList **reopenimages);
