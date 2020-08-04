/*
 * viraudit.h: auditing support
 *
 * Copyright (C) 2010-2011, 2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "virlog.h"

typedef enum {
    VIR_AUDIT_RECORD_MACHINE_CONTROL,
    VIR_AUDIT_RECORD_MACHINE_ID,
    VIR_AUDIT_RECORD_RESOURCE,
} virAuditRecordType;

int virAuditOpen(unsigned int audit_level);

void virAuditLog(bool enabled);

void virAuditSend(virLogSourcePtr source,
                  const char *filename, size_t linenr, const char *funcname,
                  const char *clienttty, const char *clientaddr,
                  virAuditRecordType type, bool success,
                  const char *fmt, ...)
    G_GNUC_PRINTF(9, 10);

char *virAuditEncode(const char *key, const char *value);

void virAuditClose(void);

#define VIR_AUDIT(type, success, ...) \
    virAuditSend(&virLogSelf, __FILE__, __LINE__, __func__, \
                 NULL, NULL, type, success, __VA_ARGS__);

#define VIR_AUDIT_USER(type, success, clienttty, clientaddr, ...) \
    virAuditSend(&virLogSelf, __FILE__, __LINE__, __func__, \
                 clienttty, clientaddr, type, success, __VA_ARGS__);

#define VIR_AUDIT_STR(str) \
    ((str) ? (str) : "?")
