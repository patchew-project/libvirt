/*
 * viruuid.h: helper APIs for dealing with UUIDs
 *
 * Copyright (C) 2007, 2011-2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"


/**
 * VIR_UUID_DEBUG:
 * @conn: connection
 * @uuid: possibly null UUID array
 */
#define VIR_UUID_DEBUG(conn, uuid) \
    do { \
        if (uuid) { \
            char _uuidstr[VIR_UUID_STRING_BUFLEN]; \
            virUUIDFormat(uuid, _uuidstr); \
            VIR_DEBUG("conn=%p, uuid=%s", conn, _uuidstr); \
        } else { \
            VIR_DEBUG("conn=%p, uuid=(null)", conn); \
        } \
    } while (0)


int virSetHostUUIDStr(const char *host_uuid);
int virGetHostUUID(unsigned char *host_uuid) ATTRIBUTE_NONNULL(1);

int virUUIDIsValid(unsigned char *uuid);

int virUUIDGenerate(unsigned char *uuid) G_GNUC_NO_INLINE;

int virUUIDParse(const char *uuidstr,
                 unsigned char *uuid)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;

const char *virUUIDFormat(const unsigned char *uuid,
                          char *uuidstr) ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);
