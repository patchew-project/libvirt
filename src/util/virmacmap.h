/*
 * virmacmap.h: MAC address <-> Domain name mapping
 *
 * Copyright (C) 2016 Red Hat, Inc.
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
 *
 * Authors:
 *     Michal Privoznik <mprivozn@redhat.com>
 */

#ifndef __VIR_MACMAP_H__
# define __VIR_MACMAP_H__

typedef struct virMACMapMgr virMACMapMgr;
typedef virMACMapMgr *virMACMapMgrPtr;

virMACMapMgrPtr virMACMapMgrNew(const char *file);

int virMACMapMgrAdd(virMACMapMgrPtr mgr,
                    const char *domain,
                    const char *mac);

int virMACMapMgrRemove(virMACMapMgrPtr mgr,
                       const char *domain,
                       const char *mac);

const char *const *virMACMapMgrLookup(virMACMapMgrPtr mgr,
                                      const char *domain);

int virMACMapMgrFlush(virMACMapMgrPtr mgr,
                      const char *filename);

int virMACMapMgrFlushStr(virMACMapMgrPtr mgr,
                         char **str);
#endif /* __VIR_MACMAPPING_H__ */
