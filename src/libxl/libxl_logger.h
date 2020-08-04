/*
 * libxl_logger.h: libxl logger implementation
 *
 * Copyright (c) 2016 SUSE LINUX Products GmbH, Nuernberg, Germany.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "util/virlog.h"

typedef struct xentoollog_logger_libvirt libxlLogger;
typedef libxlLogger *libxlLoggerPtr;

libxlLoggerPtr libxlLoggerNew(const char *logDir,
                              virLogPriority minLevel);
void libxlLoggerFree(libxlLoggerPtr logger);

void libxlLoggerOpenFile(libxlLoggerPtr logger, int id, const char *name,
                         const char *domain_config);
void libxlLoggerCloseFile(libxlLoggerPtr logger, int id);
