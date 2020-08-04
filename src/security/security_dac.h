/*
 * Copyright (C) 2010-2011 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * POSIX DAC security driver
 */

#pragma once

#include "security_driver.h"

extern virSecurityDriver virSecurityDriverDAC;

int virSecurityDACSetUserAndGroup(virSecurityManagerPtr mgr,
                                  uid_t user,
                                  gid_t group);

void virSecurityDACSetDynamicOwnership(virSecurityManagerPtr mgr,
                                       bool dynamic);

void virSecurityDACSetMountNamespace(virSecurityManagerPtr mgr,
                                     bool mountNamespace);

void virSecurityDACSetChownCallback(virSecurityManagerPtr mgr,
                                    virSecurityManagerDACChownCallback chownCallback);
