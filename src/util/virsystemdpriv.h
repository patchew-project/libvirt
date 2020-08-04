/*
 * virsystemdpriv.h: Functions for testing virSystemd APIs
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#ifndef LIBVIRT_VIRSYSTEMDPRIV_H_ALLOW
# error "virsystemdpriv.h may only be included by virsystemd.c or test suites"
#endif /* LIBVIRT_VIRSYSTEMDPRIV_H_ALLOW */

#pragma once

#include "virsystemd.h"

void virSystemdHasMachinedResetCachedValue(void);
void virSystemdHasLogindResetCachedValue(void);
