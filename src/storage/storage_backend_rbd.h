/*
 * storage_backend_rbd.h: storage backend for RBD (RADOS Block Device) handling
 *
 * Copyright (C) 2012 Wido den Hollander
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

int virStorageBackendRBDRegister(void);
