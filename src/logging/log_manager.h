/*
 * log_manager.h: log management client
 *
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

typedef struct _virLogManager virLogManager;
typedef virLogManager *virLogManagerPtr;

virLogManagerPtr virLogManagerNew(bool privileged);

void virLogManagerFree(virLogManagerPtr mgr);

int virLogManagerDomainOpenLogFile(virLogManagerPtr mgr,
                                   const char *driver,
                                   const unsigned char *domuuid,
                                   const char *domname,
                                   const char *path,
                                   unsigned int flags,
                                   ino_t *inode,
                                   off_t *offset);

int virLogManagerDomainGetLogFilePosition(virLogManagerPtr mgr,
                                          const char *path,
                                          unsigned int flags,
                                          ino_t *inode,
                                          off_t *offset);

char *virLogManagerDomainReadLogFile(virLogManagerPtr mgr,
                                     const char *path,
                                     ino_t inode,
                                     off_t offset,
                                     size_t maxlen,
                                     unsigned int flags);

int virLogManagerDomainAppendMessage(virLogManagerPtr mgr,
                                     const char *driver,
                                     const unsigned char *domuuid,
                                     const char *domname,
                                     const char *path,
                                     const char *message,
                                     unsigned int flags);
