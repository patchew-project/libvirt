/*
 * virnetworkobj.h: handle network objects
 *                  (derived from network_conf.h)
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

#ifndef __NETWORKOBJ_H__
# define __NETWORKOBJ_H__

# include "internal.h"

# include "network_conf.h"
# include "virpoolobj.h"

typedef struct _virNetworkObjPrivate virNetworkObjPrivate;
typedef virNetworkObjPrivate *virNetworkObjPrivatePtr;

pid_t virNetworkObjPrivateGetDnsmasqPid(virPoolObjPtr obj);

void virNetworkObjPrivateSetDnsmasqPid(virPoolObjPtr obj, pid_t dnsmasqPid);

pid_t virNetworkObjPrivateGetRadvdPid(virPoolObjPtr obj);

void virNetworkObjPrivateSetRadvdPid(virPoolObjPtr obj, pid_t radvdPid);

virBitmapPtr virNetworkObjPrivateGetClassId(virPoolObjPtr obj);

void virNetworkObjPrivateSetClassId(virPoolObjPtr obj, virBitmapPtr class_id);

unsigned long long virNetworkObjPrivateGetFloorSum(virPoolObjPtr obj);

void virNetworkObjPrivateSetFloorSum(virPoolObjPtr obj,
                                     unsigned long long floor_sum);

virMacMapPtr virNetworkObjPrivateGetMacMap(virPoolObjPtr obj);

void virNetworkObjPrivateSetMacMap(virPoolObjPtr obj, virMacMapPtr macmap);

unsigned int virNetworkObjPrivateGetTaint(virPoolObjPtr obj);

bool virNetworkObjPrivateIsTaint(virPoolObjPtr obj,
                                 virNetworkTaintFlags taint);

void virNetworkObjPrivateSetTaint(virPoolObjPtr obj, unsigned int taint);

virPoolObjPtr virNetworkObjAdd(virPoolObjTablePtr netobjs,
                               virNetworkDefPtr def,
                               unsigned int flags);

void virNetworkObjAssignDef(virPoolObjPtr obj,
                            virNetworkDefPtr def);

int virNetworkObjSaveStatus(const char *statusDir,
                            virPoolObjPtr obj) ATTRIBUTE_RETURN_CHECK;

int virNetworkObjDeleteConfig(const char *configDir,
                              const char *autostartDir,
                              virPoolObjPtr obj);

int virNetworkObjLoadAllConfigs(virPoolObjTablePtr netobjs,
                                const char *configDir,
                                const char *autostartDir);

int virNetworkObjLoadAllState(virPoolObjTablePtr netobjs,
                              const char *stateDir);

int virNetworkObjSetDefTransient(virPoolObjPtr obj, bool live);

void virNetworkObjUnsetDefTransient(virPoolObjPtr obj);

virNetworkDefPtr virNetworkObjGetPersistentDef(virPoolObjPtr obj);

int virNetworkObjReplacePersistentDef(virPoolObjPtr obj,
                                      virNetworkDefPtr def);

int virNetworkObjUpdate(virPoolObjPtr obj,
                        unsigned int command, /* virNetworkUpdateCommand */
                        unsigned int section, /* virNetworkUpdateSection */
                        int parentIndex,
                        const char *xml,
                        unsigned int flags);  /* virNetworkUpdateFlags */

int virNetworkObjNumOfNetworks(virPoolObjTablePtr netobjs,
                               virConnectPtr conn,
                               bool wantActive,
                               virPoolObjACLFilter aclfilter);

int virNetworkObjGetNames(virPoolObjTablePtr netobjs,
                          virConnectPtr conn,
                          bool wantActive,
                          virPoolObjACLFilter aclfilter,
                          char **const names,
                          int maxnames);

int virNetworkObjExportList(virConnectPtr conn,
                            virPoolObjTablePtr netobjs,
                            virNetworkPtr **nets,
                            virPoolObjACLFilter aclfilter,
                            unsigned int flags);

void virNetworkObjPrune(virPoolObjTablePtr netobjs,
                        unsigned int flags);

bool virNetworkObjBridgeInUse(virPoolObjTablePtr netobjs,
                              const char *bridge,
                              const char *skipname);

#endif /* __NETWORKOBJ_H__ */
