/*
 * virhostuptime.h: helper APIs for host uptime
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

int
virHostGetBootTime(unsigned long long *when)
    G_GNUC_NO_INLINE;

int
virHostBootTimeInit(void);
