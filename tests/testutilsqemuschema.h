/*
 * testutilsqemuschema.h: helper functions for QEMU QAPI schema testing
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virhash.h"
#include "virjson.h"
#include "virbuffer.h"

int
testQEMUSchemaValidate(virJSONValuePtr obj,
                       virJSONValuePtr root,
                       virHashTablePtr schema,
                       bool allowDeprecated,
                       virBufferPtr debug);

int
testQEMUSchemaValidateCommand(const char *command,
                              virJSONValuePtr arguments,
                              virHashTablePtr schema,
                              bool allowDeprecated,
                              bool allowRemoved,
                              virBufferPtr debug);

virJSONValuePtr
testQEMUSchemaGetLatest(const char* arch);

virHashTablePtr
testQEMUSchemaLoadLatest(const char *arch);

virHashTablePtr
testQEMUSchemaLoad(const char *filename);
