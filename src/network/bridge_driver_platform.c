/*
 * bridge_driver_platform.c: platform specific part of bridge driver
 *
 * Copyright (C) 2006-2013 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "bridge_driver_platform.h"

#if defined(__linux__)
# include "bridge_driver_linux.c"
#else
# include "bridge_driver_nop.c"
#endif
