/*
 * Copyright (C) 2018 Red Hat, Inc.
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

#ifndef __SECURITY_UTIL_H__
# define __SECURITY_UTIL_H__

int
virSecurityGetRememberedLabel(const char *name,
                              const char *path,
                              char **label);

int
virSecuritySetRememberedLabel(const char *name,
                              const char *path,
                              const char *label);

#endif /* __SECURITY_UTIL_H__ */
