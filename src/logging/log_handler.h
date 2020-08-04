/*
 * log_handler.h: log management daemon handler
 *
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virjson.h"

typedef struct _virLogHandler virLogHandler;
typedef virLogHandler *virLogHandlerPtr;


typedef void (*virLogHandlerShutdownInhibitor)(bool inhibit,
                                               void *opaque);

virLogHandlerPtr virLogHandlerNew(bool privileged,
                                  size_t max_size,
                                  size_t max_backups,
                                  virLogHandlerShutdownInhibitor inhibitor,
                                  void *opaque);
virLogHandlerPtr virLogHandlerNewPostExecRestart(virJSONValuePtr child,
                                                 bool privileged,
                                                 size_t max_size,
                                                 size_t max_backups,
                                                 virLogHandlerShutdownInhibitor inhibitor,
                                                 void *opaque);

void virLogHandlerFree(virLogHandlerPtr handler);

int virLogHandlerDomainOpenLogFile(virLogHandlerPtr handler,
                                   const char *driver,
                                   const unsigned char *domuuid,
                                   const char *domname,
                                   const char *path,
                                   bool trunc,
                                   ino_t *inode,
                                   off_t *offset);

int virLogHandlerDomainGetLogFilePosition(virLogHandlerPtr handler,
                                          const char *path,
                                          unsigned int flags,
                                          ino_t *inode,
                                          off_t *offset);

char *virLogHandlerDomainReadLogFile(virLogHandlerPtr handler,
                                     const char *path,
                                     ino_t inode,
                                     off_t offset,
                                     size_t maxlen,
                                     unsigned int flags);

int virLogHandlerDomainAppendLogFile(virLogHandlerPtr handler,
                                     const char *driver,
                                     const unsigned char *domuuid,
                                     const char *domname,
                                     const char *path,
                                     const char *message,
                                     unsigned int flags);

virJSONValuePtr virLogHandlerPreExecRestart(virLogHandlerPtr handler);
