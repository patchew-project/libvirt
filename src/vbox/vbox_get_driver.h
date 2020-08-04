/*
 * Copyright (C) 2014, Taowei Luo (uaedante@gmail.com)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

virHypervisorDriverPtr vboxGetHypervisorDriver(uint32_t uVersion);
virNetworkDriverPtr vboxGetNetworkDriver(uint32_t uVersion);
virStorageDriverPtr vboxGetStorageDriver(uint32_t uVersion);
