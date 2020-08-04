/*
 * cocci-macro-file.h: simplified macro definitions for Coccinelle
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * To be used with:
 *   $ spatch --macro-file scripts/cocci-macro-file.h
 */

#pragma once

#define ATTRIBUTE_NONNULL(x)
#define ATTRIBUTE_PACKED

#define G_GNUC_WARN_UNUSED_RESULT
#define G_GNUC_UNUSED
#define G_GNUC_NULL_TERMINATED
#define G_GNUC_NORETURN
#define G_GNUC_NO_INLINE
#define G_GNUC_FALLTHROUGH
#define G_GNUC_PRINTF(a, b)

#define g_autoptr(x) x##_autoptr
#define g_autofree
#define g_auto

#define BAD_CAST
