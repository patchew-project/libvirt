/*
 * virlease.h: Leases file handling
 *
 * Copyright (C) 2014 Red Hat, Inc.
 * Copyright (C) 2014 Nehal J Wani
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "virjson.h"

int virLeaseReadCustomLeaseFile(virJSONValuePtr leases_array_new,
                                const char *custom_lease_file,
                                const char *ip_to_delete,
                                char **server_duid);

int virLeasePrintLeases(virJSONValuePtr leases_array_new,
                        const char *server_duid);


int virLeaseNew(virJSONValuePtr *lease_ret,
                const char *mac,
                const char *clientid,
                const char *ip,
                const char *hostname,
                const char *iaid,
                const char *server_duid);
