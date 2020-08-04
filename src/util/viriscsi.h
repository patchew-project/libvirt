/*
 * viriscsi.h: helper APIs for managing iSCSI
 *
 * Copyright (C) 2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"

char *
virISCSIGetSession(const char *devpath,
                   bool probe)
    ATTRIBUTE_NONNULL(1);

int
virISCSIConnectionLogin(const char *portal,
                        const char *initiatoriqn,
                        const char *target)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(3)
    G_GNUC_WARN_UNUSED_RESULT;

int
virISCSIConnectionLogout(const char *portal,
                         const char *initiatoriqn,
                         const char *target)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(3)
    G_GNUC_WARN_UNUSED_RESULT;

int
virISCSIRescanLUNs(const char *session)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;

int
virISCSIScanTargets(const char *portal,
                    const char *initiatoriqn,
                    bool persist,
                    size_t *ntargetsret,
                    char ***targetsret)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;

int
virISCSINodeNew(const char *portal,
                const char *target)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2)
    G_GNUC_WARN_UNUSED_RESULT;

int
virISCSINodeUpdate(const char *portal,
                   const char *target,
                   const char *name,
                   const char *value)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3)
    ATTRIBUTE_NONNULL(4) G_GNUC_WARN_UNUSED_RESULT;
