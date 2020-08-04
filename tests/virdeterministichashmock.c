/*
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "util/virhashcode.h"

uint32_t
virHashCodeGen(const void *key,
               size_t len,
               uint32_t seed G_GNUC_UNUSED)
{
    const uint8_t *k = key;
    uint32_t h = 0;
    size_t i;

    for (i = 0; i < len; i++)
        h += k[i];

    return h;
}
