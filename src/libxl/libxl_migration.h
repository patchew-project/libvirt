/*
 * libxl_migration.h: methods for handling migration with libxenlight
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH, Nuernberg, Germany.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "libxl_conf.h"

#define LIBXL_MIGRATION_FLAGS \
    (VIR_MIGRATE_LIVE | \
     VIR_MIGRATE_PEER2PEER | \
     VIR_MIGRATE_TUNNELLED | \
     VIR_MIGRATE_PERSIST_DEST | \
     VIR_MIGRATE_UNDEFINE_SOURCE | \
     VIR_MIGRATE_PAUSED)

/* All supported migration parameters and their types. */
#define LIBXL_MIGRATION_PARAMETERS \
    VIR_MIGRATE_PARAM_URI,              VIR_TYPED_PARAM_STRING, \
    VIR_MIGRATE_PARAM_DEST_NAME,        VIR_TYPED_PARAM_STRING, \
    VIR_MIGRATE_PARAM_DEST_XML,         VIR_TYPED_PARAM_STRING, \
    NULL

char *
libxlDomainMigrationSrcBegin(virConnectPtr conn,
                             virDomainObjPtr vm,
                             const char *xmlin,
                             char **cookieout,
                             int *cookieoutlen);

virDomainDefPtr
libxlDomainMigrationDstPrepareDef(libxlDriverPrivatePtr driver,
                                  const char *dom_xml,
                                  const char *dname);

int
libxlDomainMigrationDstPrepareTunnel3(virConnectPtr dconn,
                                      virStreamPtr st,
                                      virDomainDefPtr *def,
                                      const char *cookiein,
                                      int cookieinlen,
                                      unsigned int flags);

int
libxlDomainMigrationDstPrepare(virConnectPtr dconn,
                               virDomainDefPtr *def,
                               const char *uri_in,
                               char **uri_out,
                               const char *cookiein,
                               int cookieinlen,
                               unsigned int flags);

int
libxlDomainMigrationSrcPerformP2P(libxlDriverPrivatePtr driver,
                                  virDomainObjPtr vm,
                                  virConnectPtr sconn,
                                  const char *dom_xml,
                                  const char *dconnuri,
                                  const char *uri_str,
                                  const char *dname,
                                  unsigned int flags);

int
libxlDomainMigrationSrcPerform(libxlDriverPrivatePtr driver,
                               virDomainObjPtr vm,
                               const char *dom_xml,
                               const char *dconnuri,
                               const char *uri_str,
                               const char *dname,
                               unsigned int flags);

virDomainPtr
libxlDomainMigrationDstFinish(virConnectPtr dconn,
                              virDomainObjPtr vm,
                              unsigned int flags,
                              int cancelled);

int
libxlDomainMigrationSrcConfirm(libxlDriverPrivatePtr driver,
                               virDomainObjPtr vm,
                               unsigned int flags,
                               int cancelled);
