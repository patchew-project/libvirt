/*
 * interface_driver.h: core driver methods for managing physical host interfaces
 *
 * Copyright (C) 2006, 2007 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

int interfaceRegister(void);

int netcfIfaceRegister(void);
int udevIfaceRegister(void);
