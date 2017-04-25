/*
 * virfilewrapper.h: Wrapper for universal file access
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef __VIR_FILE_MOCK_H__

int
virFileWrapperAddPrefix(const char *prefix,
                     const char *override);

void
virFileWrapperRemovePrefix(const char *prefix);

void
virFileWrapperClearPrefixes(void);

#endif /* __VIR_FILE_MOCK_H__ */
