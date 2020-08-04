/*
 * vireventglib.h: GMainContext based event loop
 *
 * Copyright (C) 2008 Daniel P. Berrange
 * Copyright (C) 2010-2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

void virEventGLibRegister(void);

int virEventGLibRunOnce(void);
