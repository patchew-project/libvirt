/*
 * virfdstream.h: generic streams impl for file descriptors
 *
 * Copyright (C) 2009-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"

/* internal callback, the generic one is used up by daemon stream driver */
/* the close callback is called with fdstream private data locked */
typedef void (*virFDStreamInternalCloseCb)(virStreamPtr st, void *opaque);

typedef void (*virFDStreamInternalCloseCbFreeOpaque)(void *opaque);


int virFDStreamOpen(virStreamPtr st,
                    int fd);

int virFDStreamConnectUNIX(virStreamPtr st,
                           const char *path,
                           bool abstract);

int virFDStreamOpenFile(virStreamPtr st,
                        const char *path,
                        unsigned long long offset,
                        unsigned long long length,
                        int oflags);
int virFDStreamCreateFile(virStreamPtr st,
                          const char *path,
                          unsigned long long offset,
                          unsigned long long length,
                          int oflags,
                          mode_t mode);
int virFDStreamOpenPTY(virStreamPtr st,
                       const char *path,
                       unsigned long long offset,
                       unsigned long long length,
                       int oflags);
int virFDStreamOpenBlockDevice(virStreamPtr st,
                               const char *path,
                               unsigned long long offset,
                               unsigned long long length,
                               bool sparse,
                               int oflags);

int virFDStreamSetInternalCloseCb(virStreamPtr st,
                                  virFDStreamInternalCloseCb cb,
                                  void *opaque,
                                  virFDStreamInternalCloseCbFreeOpaque fcb);
