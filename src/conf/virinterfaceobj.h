/*
 * virinterfaceobj.h: interface object handling entry points
 *                    (derived from interface_conf.h)
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

#ifndef __VIRINTERFACEOBJ_H__
# define __VIRINTERFACEOBJ_H__

# include "internal.h"
# include "virpoolobj.h"

int virInterfaceObjNumOfInterfaces(virPoolObjTablePtr ifaces,
                                   virConnectPtr conn,
                                   bool wantActive,
                                   virPoolObjACLFilter aclfilter);

int virInterfaceObjGetNames(virPoolObjTablePtr ifaces,
                            virConnectPtr conn,
                            bool wantActive,
                            virPoolObjACLFilter aclfilter,
                            char **const names,
                            int maxnames);

int virInterfaceObjFindByMACString(virConnectPtr conn,
                                   virPoolObjTablePtr interfaces,
                                   const char *mac,
                                   char **const matches,
                                   int maxmatches);

virPoolObjTablePtr virInterfaceObjClone(virPoolObjTablePtr src);

#endif /* __VIRINTERFACEOBJ_H__ */
