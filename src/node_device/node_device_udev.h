/*
 * node_device_udev.h: node device enumeration - libudev implementation
 *
 * Copyright (C) 2009-2010 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Author: Dave Allan <dallan@redhat.com>
 */
#ifndef __VIR_NODE_DEVICE_UDEV_H__
# define __VIR_NODE_DEVICE_UDEV_H__

# include <libudev.h>
# include <stdint.h>

# define SYSFS_DATA_SIZE 4096
# define DMI_DEVPATH "/sys/devices/virtual/dmi/id"
# define DMI_DEVPATH_FALLBACK "/sys/class/dmi/id"

int
udevPCISysfsGetMdevTypesCap(const char *sysfsPath, virNodeDevCapPCIDevPtr pci_dev);


#endif /* __VIR_NODE_DEVICE_UDEV_H__ */
