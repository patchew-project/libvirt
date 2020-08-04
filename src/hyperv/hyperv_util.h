/*
 * hyperv_util.h: utility functions for the Microsoft Hyper-V driver
 *
 * Copyright (C) 2011 Matthias Bolte <matthias.bolte@googlemail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "viruri.h"

typedef struct _hypervParsedUri hypervParsedUri;

struct _hypervParsedUri {
    char *transport;
};

int hypervParseUri(hypervParsedUri **parsedUri, virURIPtr uri);

void hypervFreeParsedUri(hypervParsedUri **parsedUri);
