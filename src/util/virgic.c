/*
 * virgic.c: ARM Generic Interrupt Controller support
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>
#include "internal.h"
#include "virgic.h"

VIR_ENUM_IMPL(virGICVersion,
              VIR_GIC_VERSION_LAST,
              "none",
              "host",
              "2",
              "3",
);
