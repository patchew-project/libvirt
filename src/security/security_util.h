/*
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

int
virSecurityGetRememberedLabel(const char *name,
                              const char *path,
                              char **label);

int
virSecuritySetRememberedLabel(const char *name,
                              const char *path,
                              const char *label);

int
virSecurityMoveRememberedLabel(const char *name,
                               const char *src,
                               const char *dst);
