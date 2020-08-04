/*
 * Copyright (C) 2012, 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

uint64_t virRandomBits(int nbits) G_GNUC_NO_INLINE;
double virRandom(void);
uint32_t virRandomInt(uint32_t max);
int virRandomBytes(unsigned char *buf, size_t buflen)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NO_INLINE;
int virRandomGenerateWWN(char **wwn, const char *virt_type) G_GNUC_NO_INLINE;
