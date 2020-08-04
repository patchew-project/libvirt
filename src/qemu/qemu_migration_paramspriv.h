/*
 * qemu_migration_paramspriv.h: private declarations for migration parameters
 *
 * Copyright (C) 2006-2018 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#ifndef LIBVIRT_QEMU_MIGRATION_PARAMSPRIV_H_ALLOW
# error "qemu_migration_paramspriv.h may only be included by qemu_migration_params.c or test suites"
#endif /* LIBVIRT_QEMU_MIGRATION_PARAMSPRIV_H_ALLOW */

#pragma once

virJSONValuePtr
qemuMigrationParamsToJSON(qemuMigrationParamsPtr migParams);

qemuMigrationParamsPtr
qemuMigrationParamsFromJSON(virJSONValuePtr params);

virJSONValuePtr
qemuMigrationCapsToJSON(virBitmapPtr caps,
                        virBitmapPtr states);
