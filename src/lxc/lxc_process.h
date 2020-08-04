/*
 * Copyright (C) 2010-2012, 2016 Red Hat, Inc.
 * Copyright IBM Corp. 2008
 *
 * lxc_process.h: LXC process lifecycle management
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "lxc_conf.h"

int virLXCProcessStart(virConnectPtr conn,
                       virLXCDriverPtr  driver,
                       virDomainObjPtr vm,
                       unsigned int nfiles, int *files,
                       bool autoDestroy,
                       virDomainRunningReason reason);
int virLXCProcessStop(virLXCDriverPtr driver,
                      virDomainObjPtr vm,
                      virDomainShutoffReason reason);

void virLXCProcessAutoDestroyRun(virLXCDriverPtr driver,
                                 virConnectPtr conn);
void virLXCProcessAutoDestroyShutdown(virLXCDriverPtr driver);
int virLXCProcessAutoDestroyAdd(virLXCDriverPtr driver,
                                virDomainObjPtr vm,
                                virConnectPtr conn);
int virLXCProcessAutoDestroyRemove(virLXCDriverPtr driver,
                                   virDomainObjPtr vm);

void virLXCProcessAutostartAll(virLXCDriverPtr driver);
int virLXCProcessReconnectAll(virLXCDriverPtr driver,
                              virDomainObjListPtr doms);

int virLXCProcessValidateInterface(virDomainNetDefPtr net);
char *virLXCProcessSetupInterfaceTap(virDomainDefPtr vm,
                                     virDomainNetDefPtr net,
                                     const char *brname);
char *virLXCProcessSetupInterfaceDirect(virLXCDriverPtr driver,
                                        virDomainDefPtr def,
                                        virDomainNetDefPtr net);
