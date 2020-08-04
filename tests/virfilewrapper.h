/*
 * virfilewrapper.h: Wrapper for universal file access
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

void
virFileWrapperAddPrefix(const char *prefix,
                        const char *override);

void
virFileWrapperRemovePrefix(const char *prefix);

void
virFileWrapperClearPrefixes(void);
