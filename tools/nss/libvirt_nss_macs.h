/*
 * libvirt_nss_macs.h: Name Service Switch plugin MAC file parser
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <sys/types.h>

int
findMACs(const char *file,
         const char *name,
         char ***macs,
         size_t *nmacs);
