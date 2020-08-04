/*
 * virfilecache.h: file caching for data
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"

#include "virobject.h"
#include "virhash.h"

typedef struct _virFileCache virFileCache;
typedef virFileCache *virFileCachePtr;

/**
 * virFileCacheIsValidPtr:
 * @data: data object to validate
 * @priv: private data created together with cache
 *
 * Validates the cached data whether it needs to be refreshed
 * or no.
 *
 * Returns *true* if it's valid or *false* if not valid.
 */
typedef bool
(*virFileCacheIsValidPtr)(void *data,
                          void *priv);

/**
 * virFileCacheNewDataPtr:
 * @name: name of the new data
 * @priv: private data created together with cache
 *
 * Creates a new data based on the @name.  The returned data must be
 * an instance of virObject.
 *
 * Returns data object or NULL on error.
 */
typedef void *
(*virFileCacheNewDataPtr)(const char *name,
                          void *priv);

/**
 * virFileCacheLoadFilePtr:
 * @filename: name of a file with cached data
 * @name: name of the cached data
 * @priv: private data created together with cache
 * @outdated: set to true if data was outdated
 *
 * Loads the cached data from a file @filename. If
 * NULL is returned, then @oudated indicates whether
 * this was due to the data being outdated, or an
 * error loading the cache.
 *
 * Returns cached data object or NULL on outdated data or error.
 */
typedef void *
(*virFileCacheLoadFilePtr)(const char *filename,
                           const char *name,
                           void *priv,
                           bool *outdated);

/**
 * virFileCacheSaveFilePtr:
 * @data: data object to save into a file
 * @filename: name of the file where to store the cached data
 * @priv: private data created together with cache
 *
 * Stores the cached to a file @filename.
 *
 * Returns 0 on success, -1 on error.
 */
typedef int
(*virFileCacheSaveFilePtr)(void *data,
                           const char *filename,
                           void *priv);

/**
 * virFileCachePrivFreePtr:
 * @priv: private data created together with cache
 *
 * This is used to free the private data when the cache object
 * is removed.
 */
typedef void
(*virFileCachePrivFreePtr)(void *priv);

typedef struct _virFileCacheHandlers virFileCacheHandlers;
typedef virFileCacheHandlers *virFileCacheHandlersPtr;
struct _virFileCacheHandlers {
    virFileCacheIsValidPtr isValid;
    virFileCacheNewDataPtr newData;
    virFileCacheLoadFilePtr loadFile;
    virFileCacheSaveFilePtr saveFile;
    virFileCachePrivFreePtr privFree;
};

virFileCachePtr
virFileCacheNew(const char *dir,
                const char *suffix,
                virFileCacheHandlers *handlers);

void *
virFileCacheLookup(virFileCachePtr cache,
                   const char *name);

void *
virFileCacheLookupByFunc(virFileCachePtr cache,
                         virHashSearcher iter,
                         const void *iterData);

void *
virFileCacheGetPriv(virFileCachePtr cache);

void
virFileCacheSetPriv(virFileCachePtr cache,
                    void *priv);

int
virFileCacheInsertData(virFileCachePtr cache,
                       const char *name,
                       void *data);
