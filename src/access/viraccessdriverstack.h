/*
 * viraccessdriverstack.h: stacked access control driver
 *
 * Copyright (C) 2012-2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "access/viraccessdriver.h"


int virAccessDriverStackAppend(virAccessManagerPtr manager,
                               virAccessManagerPtr child);

extern virAccessDriver accessDriverStack;
