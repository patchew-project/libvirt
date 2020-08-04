/*
 * qemu_qapi.h: helper functions for QEMU QAPI schema
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

#include "virhash.h"
#include "virjson.h"

int
virQEMUQAPISchemaPathGet(const char *query,
                         virHashTablePtr schema,
                         virJSONValuePtr *entry);

bool
virQEMUQAPISchemaPathExists(const char *query,
                            virHashTablePtr schema);

virHashTablePtr
virQEMUQAPISchemaConvert(virJSONValuePtr schemareply);
