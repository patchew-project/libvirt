/*
 * vireventthread.h: thread running a dedicated GMainLoop
 *
 * Copyright (C) 2020 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include <glib-object.h>

#define VIR_TYPE_EVENT_THREAD vir_event_thread_get_type()
G_DECLARE_FINAL_TYPE(virEventThread, vir_event_thread, VIR, EVENT_THREAD, GObject);

virEventThread *virEventThreadNew(const char *name);

GMainContext *virEventThreadGetContext(virEventThread *evt);
