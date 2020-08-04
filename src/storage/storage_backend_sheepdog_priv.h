/*
 * storage_backend_sheepdog_priv.h: header for functions necessary in tests
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef LIBVIRT_STORAGE_BACKEND_SHEEPDOG_PRIV_H_ALLOW
# error "storage_backend_sheepdog_priv.h may only be included by storage_backend_sheepdog.c or test suites"
#endif /* LIBVIRT_STORAGE_BACKEND_SHEEPDOG_PRIV_H_ALLOW */

#pragma once

#include "conf/storage_conf.h"

int virStorageBackendSheepdogParseNodeInfo(virStoragePoolDefPtr pool,
                                           char *output);
int virStorageBackendSheepdogParseVdiList(virStorageVolDefPtr vol,
                                          char *output);
