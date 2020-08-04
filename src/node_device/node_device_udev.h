/*
 * node_device_udev.h: node device enumeration - libudev implementation
 *
 * Copyright (C) 2009-2010 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <libudev.h>

#define SYSFS_DATA_SIZE 4096
#define DMI_DEVPATH "/sys/devices/virtual/dmi/id"
#define DMI_DEVPATH_FALLBACK "/sys/class/dmi/id"
