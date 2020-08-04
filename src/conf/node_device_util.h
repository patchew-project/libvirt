/*
 * node_device_util.h: utility functions for node device driver
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "conf/storage_adapter_conf.h"

char *
virNodeDeviceGetParentName(virConnectPtr conn,
                           const char *nodedev_name);

char *
virNodeDeviceCreateVport(virStorageAdapterFCHostPtr fchost);

int
virNodeDeviceDeleteVport(virConnectPtr conn,
                         virStorageAdapterFCHostPtr fchost);
