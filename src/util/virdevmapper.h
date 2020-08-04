/*
 * virdevmapper.h: Functions for handling device mapper
 *
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

int
virDevMapperGetTargets(const char *path,
                       char ***devPaths) G_GNUC_NO_INLINE;

bool
virIsDevMapperDevice(const char *dev_name) ATTRIBUTE_NONNULL(1);
