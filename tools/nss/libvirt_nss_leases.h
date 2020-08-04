/*
 * libvirt_nss_leases.h: Name Service Switch plugin lease file parser
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <sys/types.h>

typedef struct {
    unsigned char addr[16];
    int af;
    long long expirytime;
} leaseAddress;

int
findLeases(const char *file,
           const char *name,
           char **macs,
           size_t nmacs,
           int af,
           time_t now,
           leaseAddress **addrs,
           size_t *naddrs,
           bool *found);
