/*
 * virpidfile.h: manipulation of pidfiles
 *
 * Copyright (C) 2010-2011, 2014 Red Hat, Inc.
 * Copyright (C) 2006, 2007 Binary Karma
 * Copyright (C) 2006 Shuveb Hussain
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include <sys/types.h>
#include "internal.h"

char *virPidFileBuildPath(const char *dir,
                          const char *name);

int virPidFileWritePath(const char *path,
                        pid_t pid) G_GNUC_WARN_UNUSED_RESULT;
int virPidFileWrite(const char *dir,
                    const char *name,
                    pid_t pid) G_GNUC_WARN_UNUSED_RESULT;

int virPidFileReadPath(const char *path,
                       pid_t *pid) G_GNUC_WARN_UNUSED_RESULT;
int virPidFileRead(const char *dir,
                   const char *name,
                   pid_t *pid) G_GNUC_WARN_UNUSED_RESULT;

int virPidFileReadPathIfAlive(const char *path,
                              pid_t *pid,
                              const char *binpath) G_GNUC_WARN_UNUSED_RESULT;
int virPidFileReadIfAlive(const char *dir,
                          const char *name,
                          pid_t *pid,
                          const char *binpath) G_GNUC_WARN_UNUSED_RESULT;

int virPidFileDeletePath(const char *path);
int virPidFileDelete(const char *dir,
                     const char *name);


int virPidFileAcquirePath(const char *path,
                          bool waitForLock,
                          pid_t pid) G_GNUC_WARN_UNUSED_RESULT;
int virPidFileAcquire(const char *dir,
                      const char *name,
                      bool waitForLock,
                      pid_t pid) G_GNUC_WARN_UNUSED_RESULT;

int virPidFileReleasePath(const char *path,
                          int fd);
int virPidFileRelease(const char *dir,
                      const char *name,
                      int fd);

int virPidFileConstructPath(bool privileged,
                            const char *statedir,
                            const char *progname,
                            char **pidfile);

int virPidFileForceCleanupPath(const char *path) ATTRIBUTE_NONNULL(1);
