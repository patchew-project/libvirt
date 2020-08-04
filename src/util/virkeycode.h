/*
 * virkeycode.h: keycodes definitions and declarations
 *
 * Copyright (c) 2011 Lai Jiangshan
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "virenum.h"

VIR_ENUM_DECL(virKeycodeSet);
int virKeycodeValueFromString(virKeycodeSet codeset, const char *keyname);
int virKeycodeValueTranslate(virKeycodeSet from_codeset,
                        virKeycodeSet to_offset,
                        int key_value);
