/*
 * viridentity.h: helper APIs for managing user identities
 *
 * Copyright (C) 2012-2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include <glib-object.h>

#define VIR_TYPE_IDENTITY vir_identity_get_type()
G_DECLARE_FINAL_TYPE(virIdentity, vir_identity, VIR, IDENTITY, GObject);

typedef virIdentity *virIdentityPtr;

virIdentityPtr virIdentityGetCurrent(void);
int virIdentitySetCurrent(virIdentityPtr ident);

virIdentityPtr virIdentityGetSystem(void);

virIdentityPtr virIdentityNew(void);

int virIdentityGetUserName(virIdentityPtr ident,
                           const char **username);
int virIdentityGetUNIXUserID(virIdentityPtr ident,
                             uid_t *uid);
int virIdentityGetGroupName(virIdentityPtr ident,
                            const char **groupname);
int virIdentityGetUNIXGroupID(virIdentityPtr ident,
                              gid_t *gid);
int virIdentityGetProcessID(virIdentityPtr ident,
                            pid_t *pid);
int virIdentityGetProcessTime(virIdentityPtr ident,
                              unsigned long long *timestamp);
int virIdentityGetSASLUserName(virIdentityPtr ident,
                               const char **username);
int virIdentityGetX509DName(virIdentityPtr ident,
                            const char **dname);
int virIdentityGetSELinuxContext(virIdentityPtr ident,
                                 const char **context);


int virIdentitySetUserName(virIdentityPtr ident,
                           const char *username);
int virIdentitySetUNIXUserID(virIdentityPtr ident,
                             uid_t uid);
int virIdentitySetGroupName(virIdentityPtr ident,
                            const char *groupname);
int virIdentitySetUNIXGroupID(virIdentityPtr ident,
                              gid_t gid);
int virIdentitySetProcessID(virIdentityPtr ident,
                            pid_t pid);
int virIdentitySetProcessTime(virIdentityPtr ident,
                              unsigned long long timestamp);
int virIdentitySetSASLUserName(virIdentityPtr ident,
                               const char *username);
int virIdentitySetX509DName(virIdentityPtr ident,
                            const char *dname);
int virIdentitySetSELinuxContext(virIdentityPtr ident,
                                 const char *context);

int virIdentitySetParameters(virIdentityPtr ident,
                             virTypedParameterPtr params,
                             int nparams);

int virIdentityGetParameters(virIdentityPtr ident,
                             virTypedParameterPtr *params,
                             int *nparams);
