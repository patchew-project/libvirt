/*
 * Copyright (C) 2011-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "domain_conf.h"
#include "qemu/qemu_conf.h"
#include "qemu/qemu_monitor.h"
#include "qemu/qemu_agent.h"

typedef struct _qemuMonitorTest qemuMonitorTest;
typedef qemuMonitorTest *qemuMonitorTestPtr;

typedef struct _qemuMonitorTestItem qemuMonitorTestItem;
typedef qemuMonitorTestItem *qemuMonitorTestItemPtr;
typedef int (*qemuMonitorTestResponseCallback)(qemuMonitorTestPtr test,
                                               qemuMonitorTestItemPtr item,
                                               const char *message);

int qemuMonitorTestAddHandler(qemuMonitorTestPtr test,
                              const char *identifier,
                              qemuMonitorTestResponseCallback cb,
                              void *opaque,
                              virFreeCallback freecb);

int qemuMonitorTestAddResponse(qemuMonitorTestPtr test,
                               const char *response);

int qemuMonitorTestAddInvalidCommandResponse(qemuMonitorTestPtr test,
                                             const char *expectedcommand,
                                             const char *actualcommand);

void *qemuMonitorTestItemGetPrivateData(qemuMonitorTestItemPtr item);

int qemuMonitorTestAddErrorResponse(qemuMonitorTestPtr test, const char *errmsg, ...);

void qemuMonitorTestAllowUnusedCommands(qemuMonitorTestPtr test);
void qemuMonitorTestSkipDeprecatedValidation(qemuMonitorTestPtr test,
                                             bool allowRemoved);

int qemuMonitorTestAddItem(qemuMonitorTestPtr test,
                           const char *command_name,
                           const char *response);

int qemuMonitorTestAddItemVerbatim(qemuMonitorTestPtr test,
                                   const char *command,
                                   const char *cmderr,
                                   const char *response);

int qemuMonitorTestAddAgentSyncResponse(qemuMonitorTestPtr test);

int qemuMonitorTestAddItemParams(qemuMonitorTestPtr test,
                                 const char *cmdname,
                                 const char *response,
                                 ...)
    G_GNUC_NULL_TERMINATED;

int qemuMonitorTestAddItemExpect(qemuMonitorTestPtr test,
                                 const char *cmdname,
                                 const char *cmdargs,
                                 bool apostrophe,
                                 const char *response);

#define qemuMonitorTestNewSimple(xmlopt) \
    qemuMonitorTestNew(xmlopt, NULL, NULL, NULL, NULL)
#define qemuMonitorTestNewSchema(xmlopt, schema) \
    qemuMonitorTestNew(xmlopt, NULL, NULL, NULL, schema)

qemuMonitorTestPtr qemuMonitorTestNew(virDomainXMLOptionPtr xmlopt,
                                      virDomainObjPtr vm,
                                      virQEMUDriverPtr driver,
                                      const char *greeting,
                                      virHashTablePtr schema);

qemuMonitorTestPtr qemuMonitorTestNewFromFile(const char *fileName,
                                              virDomainXMLOptionPtr xmlopt,
                                              bool simple);
qemuMonitorTestPtr qemuMonitorTestNewFromFileFull(const char *fileName,
                                                  virQEMUDriverPtr driver,
                                                  virDomainObjPtr vm,
                                                  virHashTablePtr qmpschema);

qemuMonitorTestPtr qemuMonitorTestNewAgent(virDomainXMLOptionPtr xmlopt);


void qemuMonitorTestFree(qemuMonitorTestPtr test);

qemuMonitorPtr qemuMonitorTestGetMonitor(qemuMonitorTestPtr test);
qemuAgentPtr qemuMonitorTestGetAgent(qemuMonitorTestPtr test);
virDomainObjPtr qemuMonitorTestGetDomainObj(qemuMonitorTestPtr test);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(qemuMonitorTest, qemuMonitorTestFree);
