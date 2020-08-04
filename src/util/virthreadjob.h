/*
 * virthreadjob.h: APIs for tracking job associated with current thread
 *
 * Copyright (C) 2013-2015 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

const char *virThreadJobGet(void);

void virThreadJobSetWorker(const char *caller);
void virThreadJobSet(const char *caller);
void virThreadJobClear(int rv);
