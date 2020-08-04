/*
 * virhashcode.h: hash code generation
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The hash code generation is based on the public domain MurmurHash3 from Austin Appleby:
 * http://code.google.com/p/smhasher/source/browse/trunk/MurmurHash3.cpp
 *
 * We use only the 32 bit variant because the 2 produce different result while
 * we need to produce the same result regardless of the architecture as
 * clients can be both 64 or 32 bit at the same time.
 */

#pragma once

#include "internal.h"

uint32_t virHashCodeGen(const void *key, size_t len, uint32_t seed)
    G_GNUC_NO_INLINE;
