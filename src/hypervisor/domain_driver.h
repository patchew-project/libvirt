/*
 * domain_driver.h: general functions shared between hypervisor drivers
 *
 * Copyright IBM Corp. 2020
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "domain_conf.h"

char *
virDomainDriverGenerateRootHash(const char *drivername,
                                const char *root);

char *
virDomainDriverGenerateMachineName(const char *drivername,
                                   const char *root,
                                   int id,
                                   const char *name,
                                   bool privileged);

int virDomainDriverMergeBlkioDevice(virBlkioDevicePtr *dest_array,
                                    size_t *dest_size,
                                    virBlkioDevicePtr src_array,
                                    size_t src_size,
                                    const char *type);

int virDomainDriverParseBlkioDeviceStr(char *blkioDeviceStr, const char *type,
                                       virBlkioDevicePtr *dev, size_t *size);

int virDomainDriverSetupPersistentDefBlkioParams(virDomainDefPtr persistentDef,
                                                 virTypedParameterPtr params,
                                                 int nparams);
