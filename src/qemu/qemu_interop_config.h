/*
 * qemu_interop_config.h: QEMU firmware/vhost-user etc configs
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

int qemuInteropFetchConfigs(const char *name, char ***configs, bool privileged);
