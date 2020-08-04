/*
 * vireventglibwatch.h: GSource impl for sockets
 *
 * Copyright (C) 2015-2020 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

/**
 * virEventGLibCreateSocketWatch:
 * @fd: the file descriptor
 * @condition: the I/O condition
 *
 * Create a new main loop source that is able to
 * monitor the file descriptor @fd for the
 * I/O conditions in @condition.
 *
 * Returns: the new main loop source
 */
GSource *virEventGLibCreateSocketWatch(int fd,
                                       GIOCondition condition);

typedef gboolean (*virEventGLibSocketFunc)(int fd,
                                           GIOCondition condition,
                                           gpointer data);

guint virEventGLibAddSocketWatch(int fd,
                                 GIOCondition condition,
                                 GMainContext *context,
                                 virEventGLibSocketFunc func,
                                 gpointer opaque,
                                 GDestroyNotify notify);
