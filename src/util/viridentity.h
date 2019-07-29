/*
 * viridentity.h: helper APIs for managing user identities
 *
 * Copyright (C) 2012-2013 Red Hat, Inc.
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
 * License along with this library;  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include "virobject.h"

typedef struct _virIdentity virIdentity;
typedef virIdentity *virIdentityPtr;

typedef enum {
      VIR_IDENTITY_ATTR_OS_USER_NAME,
      VIR_IDENTITY_ATTR_OS_USER_ID,
      VIR_IDENTITY_ATTR_OS_GROUP_NAME,
      VIR_IDENTITY_ATTR_OS_GROUP_ID,
      VIR_IDENTITY_ATTR_OS_PROCESS_ID,
      VIR_IDENTITY_ATTR_OS_PROCESS_TIME,
      VIR_IDENTITY_ATTR_SASL_USER_NAME,
      VIR_IDENTITY_ATTR_X509_DISTINGUISHED_NAME,
      VIR_IDENTITY_ATTR_SELINUX_CONTEXT,

      VIR_IDENTITY_ATTR_LAST,
} virIdentityAttrType;

virIdentityPtr virIdentityGetCurrent(void);
int virIdentitySetCurrent(virIdentityPtr ident);

virIdentityPtr virIdentityGetSystem(void);

virIdentityPtr virIdentityNew(void);

int virIdentitySetAttr(virIdentityPtr ident,
                       unsigned int attr,
                       const char *value)
    ATTRIBUTE_NONNULL(1)
    ATTRIBUTE_NONNULL(3);

int virIdentityGetAttr(virIdentityPtr ident,
                       unsigned int attr,
                       const char **value)
    ATTRIBUTE_NONNULL(1)
    ATTRIBUTE_NONNULL(3);

bool virIdentityIsEqual(virIdentityPtr identA,
                        virIdentityPtr identB)
    ATTRIBUTE_NONNULL(1)
    ATTRIBUTE_NONNULL(2);

int virIdentityGetOSUserName(virIdentityPtr ident,
                             const char **username);
int virIdentityGetOSUserID(virIdentityPtr ident,
                           uid_t *uid);
int virIdentityGetOSGroupName(virIdentityPtr ident,
                              const char **groupname);
int virIdentityGetOSGroupID(virIdentityPtr ident,
                            gid_t *gid);
int virIdentityGetOSProcessID(virIdentityPtr ident,
                              pid_t *pid);
int virIdentityGetOSProcessTime(virIdentityPtr ident,
                                unsigned long long *timestamp);
int virIdentityGetSASLUserName(virIdentityPtr ident,
                               const char **username);
int virIdentityGetX509DName(virIdentityPtr ident,
                            const char **dname);
int virIdentityGetSELinuxContext(virIdentityPtr ident,
                                 const char **context);


int virIdentitySetOSUserName(virIdentityPtr ident,
                             const char *username);
int virIdentitySetOSUserID(virIdentityPtr ident,
                           uid_t uid);
int virIdentitySetOSGroupName(virIdentityPtr ident,
                              const char *groupname);
int virIdentitySetOSGroupID(virIdentityPtr ident,
                            gid_t gid);
int virIdentitySetOSProcessID(virIdentityPtr ident,
                              pid_t pid);
int virIdentitySetOSProcessTime(virIdentityPtr ident,
                                unsigned long long timestamp);
int virIdentitySetSASLUserName(virIdentityPtr ident,
                               const char *username);
int virIdentitySetX509DName(virIdentityPtr ident,
                            const char *dname);
int virIdentitySetSELinuxContext(virIdentityPtr ident,
                                 const char *context);
