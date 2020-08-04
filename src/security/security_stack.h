/*
 * Copyright (C) 2010-2011 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Stacked security driver
 */

#pragma once

#include "security_driver.h"

extern virSecurityDriver virSecurityDriverStack;


int
virSecurityStackAddNested(virSecurityManagerPtr mgr,
                          virSecurityManagerPtr nested);
virSecurityManagerPtr
virSecurityStackGetPrimary(virSecurityManagerPtr mgr);

virSecurityManagerPtr*
virSecurityStackGetNested(virSecurityManagerPtr mgr);
