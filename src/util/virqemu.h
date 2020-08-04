/*
 * virqemu.h: utilities for working with qemu and its tools
 *
 * Copyright (C) 2009, 2012-2016 Red Hat, Inc.
 * Copyright (C) 2009 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "virbuffer.h"
#include "virjson.h"
#include "virstorageencryption.h"

typedef int (*virQEMUBuildCommandLineJSONArrayFormatFunc)(const char *key,
                                                          virJSONValuePtr array,
                                                          virBufferPtr buf,
                                                          const char *skipKey,
                                                          bool onOff);
int virQEMUBuildCommandLineJSONArrayBitmap(const char *key,
                                           virJSONValuePtr array,
                                           virBufferPtr buf,
                                           const char *skipKey,
                                           bool onOff);
int virQEMUBuildCommandLineJSONArrayNumbered(const char *key,
                                             virJSONValuePtr array,
                                             virBufferPtr buf,
                                             const char *skipKey,
                                             bool onOff);

int virQEMUBuildCommandLineJSON(virJSONValuePtr value,
                                virBufferPtr buf,
                                const char *skipKey,
                                bool onOff,
                                virQEMUBuildCommandLineJSONArrayFormatFunc array);

char *
virQEMUBuildNetdevCommandlineFromJSON(virJSONValuePtr props,
                                      bool rawjson);

int virQEMUBuildObjectCommandlineFromJSON(virBufferPtr buf,
                                          virJSONValuePtr objprops);

char *virQEMUBuildDriveCommandlineFromJSON(virJSONValuePtr src);

void virQEMUBuildBufferEscapeComma(virBufferPtr buf, const char *str);
void virQEMUBuildQemuImgKeySecretOpts(virBufferPtr buf,
                                      virStorageEncryptionInfoDefPtr enc,
                                      const char *alias)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3);
