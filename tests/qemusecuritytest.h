/*
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#define ENVVAR "LIBVIRT_QEMU_SECURITY_TEST"

extern int checkPaths(const char **paths);

extern void freePaths(void);
