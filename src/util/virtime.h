/*
 * virtime.h: Time handling functions
 *
 * Copyright (C) 2006-2011, 2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <time.h>

#include "internal.h"

/* The format string we intend to use is:
 *
 * Yr  Mon  Day  Hour  Min  Sec Ms   TZ
 * %4d-%02d-%02d %02d:%02d:%02d.%03d+0000
 *
 */
#define VIR_TIME_STRING_BUFLEN \
    (4 + 1 + 2 + 1 + 2 + 1 + 2 + 1 + 2 + 1 + 2 + 1 + 3 + 5 + 1)
/*   Yr      Mon     Day     Hour    Min     Sec     Ms  TZ  NULL */

void virTimeFieldsThen(unsigned long long when, struct tm *fields)
    ATTRIBUTE_NONNULL(2);

/* These APIs are async signal safe and return -1, setting
 * errno on failure */
int virTimeMillisNowRaw(unsigned long long *now)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;
int virTimeFieldsNowRaw(struct tm *fields)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;
int virTimeStringNowRaw(char *buf)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;
int virTimeStringThenRaw(unsigned long long when, char *buf)
    ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;

/* These APIs are *not* async signal safe and return -1,
 * raising a libvirt error on failure
 */
int virTimeMillisNow(unsigned long long *now)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;
int virTimeFieldsNow(struct tm *fields)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;
char *virTimeStringNow(void);
char *virTimeStringThen(unsigned long long when);

int virTimeLocalOffsetFromUTC(long *offset)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;

typedef struct {
    unsigned long long start_t;
    unsigned long long next;
    unsigned long long limit_t;
} virTimeBackOffVar;

int virTimeBackOffStart(virTimeBackOffVar *var,
                        unsigned long long first, unsigned long long timeout);

bool virTimeBackOffWait(virTimeBackOffVar *var);
