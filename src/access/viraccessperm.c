/*
 * viraccessperm.c: access control permissions
 *
 * Copyright (C) 2012-2014 Red Hat, Inc.
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
 */

#include <config.h>

#include "viraccessperm.h"


VIR_ENUM_IMPL(virAccessPermConnect, NULL,
              VIR_ACCESS_PERM_CONNECT_LAST,
              "getattr", "read", "write",
              "search_domains", "search_networks",
              "search_storage_pools", "search_node_devices",
              "search_interfaces", "search_secrets",
              "search_nwfilters", "search_nwfilter_bindings",
              "detect_storage_pools", "pm_control",
              "interface_transaction",
);

VIR_ENUM_IMPL(virAccessPermDomain, NULL,
              VIR_ACCESS_PERM_DOMAIN_LAST,
              "getattr", "read", "write", "read_secure",
              "start", "stop", "reset",
              "save", "delete",
              "migrate", "snapshot", "suspend", "hibernate", "core_dump", "pm_control",
              "init_control", "inject_nmi", "send_input", "send_signal",
              "fs_trim", "fs_freeze",
              "block_read", "block_write", "mem_read",
              "open_graphics", "open_device", "screenshot",
              "open_namespace", "set_time", "set_password",
);

VIR_ENUM_IMPL(virAccessPermInterface, NULL,
              VIR_ACCESS_PERM_INTERFACE_LAST,
              "getattr", "read", "write", "save",
              "delete", "start", "stop",
);

VIR_ENUM_IMPL(virAccessPermNetwork, NULL,
              VIR_ACCESS_PERM_NETWORK_LAST,
              "getattr", "read", "write",
              "save", "delete", "start", "stop",
);

VIR_ENUM_IMPL(virAccessPermNodeDevice, NULL,
              VIR_ACCESS_PERM_NODE_DEVICE_LAST,
              "getattr", "read", "write",
              "start", "stop",
              "detach",
);

VIR_ENUM_IMPL(virAccessPermNWFilter, NULL,
              VIR_ACCESS_PERM_NWFILTER_LAST,
              "getattr", "read", "write",
              "save", "delete",
);

VIR_ENUM_IMPL(virAccessPermNWFilterBinding, NULL,
              VIR_ACCESS_PERM_NWFILTER_BINDING_LAST,
              "getattr", "read",
              "create", "delete",
);

VIR_ENUM_IMPL(virAccessPermSecret, NULL,
              VIR_ACCESS_PERM_SECRET_LAST,
              "getattr", "read", "write",
              "read_secure", "save", "delete",
);

VIR_ENUM_IMPL(virAccessPermStoragePool, NULL,
              VIR_ACCESS_PERM_STORAGE_POOL_LAST,
              "getattr", "read", "write",
              "save", "delete", "start", "stop",
              "refresh", "search_storage_vols",
              "format",
);

VIR_ENUM_IMPL(virAccessPermStorageVol, NULL,
              VIR_ACCESS_PERM_STORAGE_VOL_LAST,
              "getattr", "read", "create", "delete",
              "format", "resize", "data_read",
              "data_write",
);
