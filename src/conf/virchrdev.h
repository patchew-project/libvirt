/**
 * virchrdev.h: api to guarantee mutually exclusive
 * access to domain's character devices
 *
 * Copyright (C) 2011-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "domain_conf.h"

typedef struct _virChrdevs virChrdevs;
typedef virChrdevs *virChrdevsPtr;

virChrdevsPtr virChrdevAlloc(void);
void virChrdevFree(virChrdevsPtr devs);

int virChrdevOpen(virChrdevsPtr devs, virDomainChrSourceDefPtr source,
                  virStreamPtr st, bool force);
