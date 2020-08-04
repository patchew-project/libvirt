/*
 * viruri.h: internal definitions used for URI parsing.
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virconf.h"

typedef struct _virURI virURI;
typedef virURI *virURIPtr;

typedef struct _virURIParam virURIParam;
typedef virURIParam *virURIParamPtr;

struct _virURIParam {
    char *name;  /* Name (unescaped). */
    char *value; /* Value (unescaped). */
    bool ignore; /* Ignore this field in virURIFormatParams */
};

struct _virURI {
    char *scheme;       /* the URI scheme */
    char *server;       /* the server part */
    char *user;         /* the user part */
    unsigned int port;  /* the port number */
    char *path;         /* the path string */
    char *query;        /* the query string */
    char *fragment;     /* the fragment string */

    size_t paramsCount;
    size_t paramsAlloc;
    virURIParamPtr params;
};

virURIPtr virURIParse(const char *uri)
    ATTRIBUTE_NONNULL(1);
char *virURIFormat(virURIPtr uri)
    ATTRIBUTE_NONNULL(1);

char *virURIFormatParams(virURIPtr uri);

void virURIFree(virURIPtr uri);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(virURI, virURIFree);
int virURIResolveAlias(virConfPtr conf, const char *alias, char **uri);

const char *virURIGetParam(virURIPtr uri, const char *name);

#define VIR_URI_SERVER(uri) ((uri) && (uri)->server ? (uri)->server : "localhost")
