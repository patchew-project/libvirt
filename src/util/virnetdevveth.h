/*
 * Copyright (C) 2010-2011 Red Hat, Inc.
 * Copyright IBM Corp. 2008
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

/* Function declarations */
int virNetDevVethCreate(char **veth1, char **veth2)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;
int virNetDevVethDelete(const char *veth)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;
