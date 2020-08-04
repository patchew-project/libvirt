/*
 * bhyve_driver.h: core driver methods for managing bhyve guests
 *
 * Copyright (C) 2014 Roman Bogorodskiy
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "capabilities.h"
#include "bhyve_utils.h"

int bhyveRegister(void);

unsigned bhyveDriverGetBhyveCaps(bhyveConnPtr driver);

unsigned bhyveDriverGetGrubCaps(bhyveConnPtr driver);

virCapsPtr bhyveDriverGetCapabilities(bhyveConnPtr driver);
