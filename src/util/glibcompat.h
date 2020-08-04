/*
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>
#include <glib/gstdio.h>

gchar * vir_g_canonicalize_filename(const gchar *filename,
                                    const gchar *relative_to);
gint vir_g_fsync(gint fd);
char *vir_g_strdup_printf(const char *msg, ...)
    G_GNUC_PRINTF(1, 2);
char *vir_g_strdup_vprintf(const char *msg, va_list args)
    G_GNUC_PRINTF(1, 0);

#if !GLIB_CHECK_VERSION(2, 64, 0)
# define g_strdup_printf vir_g_strdup_printf
# define g_strdup_vprintf vir_g_strdup_vprintf
#endif

#define g_canonicalize_filename vir_g_canonicalize_filename
#undef g_fsync
#define g_fsync vir_g_fsync
