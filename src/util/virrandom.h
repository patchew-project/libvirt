/*
 * Copyright (C) 2012, 2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "internal.h"

uint64_t virRandomBits(int nbits) G_GNUC_NO_INLINE;
double virRandom(void);
uint32_t virRandomInt(uint32_t max);
int virRandomBytes(unsigned char *buf, size_t buflen)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NO_INLINE;
int virRandomGenerateWWN(char **wwn, const char *virt_type) G_GNUC_NO_INLINE;
