/*
 * virauth.h: authentication related utility functions
 *
 * Copyright (C) 2013 Red Hat, Inc.
 * Copyright (C) 2010 Matthias Bolte <matthias.bolte@googlemail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "viruri.h"

int virAuthGetConfigFilePath(virConnectPtr conn,
                             char **path);

int virAuthGetConfigFilePathURI(virURIPtr uri,
                                char **path);


char *virAuthGetUsername(virConnectPtr conn,
                         virConnectAuthPtr auth,
                         const char *servicename,
                         const char *defaultUsername,
                         const char *hostname);
char *virAuthGetPassword(virConnectPtr conn,
                         virConnectAuthPtr auth,
                         const char *servicename,
                         const char *username,
                         const char *hostname);
char * virAuthGetUsernamePath(const char *path,
                              virConnectAuthPtr auth,
                              const char *servicename,
                              const char *defaultUsername,
                              const char *hostname);
char * virAuthGetPasswordPath(const char *path,
                              virConnectAuthPtr auth,
                              const char *servicename,
                              const char *username,
                              const char *hostname);
