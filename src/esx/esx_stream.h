/*
 * esx_stream.h: libcurl based stream driver
 *
 * Copyright (C) 2012-2014 Matthias Bolte <matthias.bolte@googlemail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "esx_private.h"

int esxStreamOpenUpload(virStreamPtr stream, esxPrivate *priv, const char *url);
int esxStreamOpenDownload(virStreamPtr stream, esxPrivate *priv, const char *url,
                          unsigned long long offset, unsigned long long length);
