/*
 * virfcp.h: Utility functions for the Fibre Channel Protocol
 *
 * Copyright (C) 2017 IBM Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

bool
virFCIsCapableRport(const char *rport);

int
virFCReadRportValue(const char *rport,
                    const char *entry,
                    char **result);
