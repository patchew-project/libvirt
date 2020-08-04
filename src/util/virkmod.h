/*
 * virkmod.h: helper APIs for managing kernel modprobe
 *
 * Copyright (C) 2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"

char *virKModLoad(const char *)
    ATTRIBUTE_NONNULL(1);
char *virKModUnload(const char *)
    ATTRIBUTE_NONNULL(1);
bool virKModIsProhibited(const char *)
    ATTRIBUTE_NONNULL(1);
