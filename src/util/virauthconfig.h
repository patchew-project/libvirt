/*
 * virauthconfig.h: authentication config handling
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

typedef struct _virAuthConfig virAuthConfig;
typedef virAuthConfig *virAuthConfigPtr;


virAuthConfigPtr virAuthConfigNew(const char *path);
virAuthConfigPtr virAuthConfigNewData(const char *path,
                                      const char *data,
                                      size_t len);

void virAuthConfigFree(virAuthConfigPtr auth);

int virAuthConfigLookup(virAuthConfigPtr auth,
                        const char *service,
                        const char *hostname,
                        const char *credname,
                        char **value);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virAuthConfig, virAuthConfigFree);
