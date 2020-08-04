/*
 * domain_cgroup.h: cgroup functions shared between hypervisor drivers
 *
 * Copyright IBM Corp. 2020
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vircgroup.h"
#include "domain_conf.h"


int virDomainCgroupSetupBlkio(virCgroupPtr cgroup, virDomainBlkiotune blkio);
int virDomainCgroupSetupMemtune(virCgroupPtr cgroup, virDomainMemtune mem);
int virDomainCgroupSetupDomainBlkioParameters(virCgroupPtr cgroup,
                                              virDomainDefPtr def,
                                              virTypedParameterPtr params,
                                              int nparams);
int virDomainCgroupSetMemoryLimitParameters(virCgroupPtr cgroup,
                                            virDomainObjPtr vm,
                                            virDomainDefPtr liveDef,
                                            virDomainDefPtr persistentDef,
                                            virTypedParameterPtr params,
                                            int nparams);
