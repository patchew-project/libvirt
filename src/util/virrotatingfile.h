/*
 * virrotatingfile.h: reading/writing of auto-rotating files
 *
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"

typedef struct virRotatingFileWriter virRotatingFileWriter;
typedef virRotatingFileWriter *virRotatingFileWriterPtr;

typedef struct virRotatingFileReader virRotatingFileReader;
typedef virRotatingFileReader *virRotatingFileReaderPtr;

virRotatingFileWriterPtr virRotatingFileWriterNew(const char *path,
                                                  off_t maxlen,
                                                  size_t maxbackup,
                                                  bool trunc,
                                                  mode_t mode);

virRotatingFileReaderPtr virRotatingFileReaderNew(const char *path,
                                                  size_t maxbackup);

const char *virRotatingFileWriterGetPath(virRotatingFileWriterPtr file);

ino_t virRotatingFileWriterGetINode(virRotatingFileWriterPtr file);
off_t virRotatingFileWriterGetOffset(virRotatingFileWriterPtr file);

ssize_t virRotatingFileWriterAppend(virRotatingFileWriterPtr file,
                                    const char *buf,
                                    size_t len);

int virRotatingFileReaderSeek(virRotatingFileReaderPtr file,
                              ino_t inode,
                              off_t offset);

ssize_t virRotatingFileReaderConsume(virRotatingFileReaderPtr file,
                                     char *buf,
                                     size_t len);

void virRotatingFileWriterFree(virRotatingFileWriterPtr file);
void virRotatingFileReaderFree(virRotatingFileReaderPtr file);
