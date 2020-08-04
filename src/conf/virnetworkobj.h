/*
 * virnetworkobj.h: handle network objects
 *                  (derived from network_conf.h)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

#include "network_conf.h"
#include "virnetworkportdef.h"

typedef struct _virNetworkObj virNetworkObj;
typedef virNetworkObj *virNetworkObjPtr;

virNetworkObjPtr
virNetworkObjNew(void);

virNetworkDefPtr
virNetworkObjGetDef(virNetworkObjPtr obj);

void
virNetworkObjSetDef(virNetworkObjPtr obj,
                    virNetworkDefPtr def);

virNetworkDefPtr
virNetworkObjGetNewDef(virNetworkObjPtr obj);

bool
virNetworkObjIsActive(virNetworkObjPtr obj);

void
virNetworkObjSetActive(virNetworkObjPtr obj,
                       bool active);

bool
virNetworkObjIsPersistent(virNetworkObjPtr obj);

bool
virNetworkObjIsAutostart(virNetworkObjPtr obj);

void
virNetworkObjSetAutostart(virNetworkObjPtr obj,
                          bool autostart);

virMacMapPtr
virNetworkObjGetMacMap(virNetworkObjPtr obj);

pid_t
virNetworkObjGetDnsmasqPid(virNetworkObjPtr obj);

void
virNetworkObjSetDnsmasqPid(virNetworkObjPtr obj,
                           pid_t dnsmasqPid);

pid_t
virNetworkObjGetRadvdPid(virNetworkObjPtr obj);

void
virNetworkObjSetRadvdPid(virNetworkObjPtr obj,
                         pid_t radvdPid);

virBitmapPtr
virNetworkObjGetClassIdMap(virNetworkObjPtr obj);

unsigned long long
virNetworkObjGetFloorSum(virNetworkObjPtr obj);

void
virNetworkObjSetFloorSum(virNetworkObjPtr obj,
                         unsigned long long floor_sum);

void
virNetworkObjSetMacMap(virNetworkObjPtr obj,
                       virMacMapPtr macmap);

void
virNetworkObjUnrefMacMap(virNetworkObjPtr obj);

int
virNetworkObjMacMgrAdd(virNetworkObjPtr obj,
                       const char *dnsmasqStateDir,
                       const char *domain,
                       const virMacAddr *mac);

int
virNetworkObjMacMgrDel(virNetworkObjPtr obj,
                       const char *dnsmasqStateDir,
                       const char *domain,
                       const virMacAddr *mac);

void
virNetworkObjEndAPI(virNetworkObjPtr *net);

typedef struct _virNetworkObjList virNetworkObjList;
typedef virNetworkObjList *virNetworkObjListPtr;

virNetworkObjListPtr
virNetworkObjListNew(void);

virNetworkObjPtr
virNetworkObjFindByUUID(virNetworkObjListPtr nets,
                        const unsigned char *uuid);

virNetworkObjPtr
virNetworkObjFindByName(virNetworkObjListPtr nets,
                        const char *name);

bool
virNetworkObjTaint(virNetworkObjPtr obj,
                   virNetworkTaintFlags taint);

typedef bool
(*virNetworkObjListFilter)(virConnectPtr conn,
                           virNetworkDefPtr def);

virNetworkObjPtr
virNetworkObjAssignDef(virNetworkObjListPtr nets,
                       virNetworkDefPtr def,
                       unsigned int flags);

void
virNetworkObjUpdateAssignDef(virNetworkObjPtr network,
                             virNetworkDefPtr def,
                             bool live);

int
virNetworkObjSetDefTransient(virNetworkObjPtr network,
                             bool live,
                             virNetworkXMLOptionPtr xmlopt);

void
virNetworkObjUnsetDefTransient(virNetworkObjPtr network);

virNetworkDefPtr
virNetworkObjGetPersistentDef(virNetworkObjPtr network);

int
virNetworkObjReplacePersistentDef(virNetworkObjPtr network,
                                  virNetworkDefPtr def);

void
virNetworkObjRemoveInactive(virNetworkObjListPtr nets,
                            virNetworkObjPtr net);

int
virNetworkObjAddPort(virNetworkObjPtr net,
                     virNetworkPortDefPtr portdef,
                     const char *stateDir);

char *
virNetworkObjGetPortStatusDir(virNetworkObjPtr net,
                              const char *stateDir);

virNetworkPortDefPtr
virNetworkObjLookupPort(virNetworkObjPtr net,
                        const unsigned char *uuid);

int
virNetworkObjDeletePort(virNetworkObjPtr net,
                        const unsigned char *uuid,
                        const char *stateDir);

int
virNetworkObjDeleteAllPorts(virNetworkObjPtr net,
                            const char *stateDir);

typedef bool
(*virNetworkPortListFilter)(virConnectPtr conn,
                            virNetworkDefPtr def,
                            virNetworkPortDefPtr portdef);

int
virNetworkObjPortListExport(virNetworkPtr net,
                            virNetworkObjPtr obj,
                            virNetworkPortPtr **ports,
                            virNetworkPortListFilter filter);

typedef bool
(*virNetworkPortListIter)(virNetworkPortDefPtr portdef,
                          void *opaque);

int
virNetworkObjPortForEach(virNetworkObjPtr obj,
                         virNetworkPortListIter iter,
                         void *opaque);

int
virNetworkObjSaveStatus(const char *statusDir,
                        virNetworkObjPtr net,
                        virNetworkXMLOptionPtr xmlopt) G_GNUC_WARN_UNUSED_RESULT;

int
virNetworkObjLoadAllConfigs(virNetworkObjListPtr nets,
                            const char *configDir,
                            const char *autostartDir,
                            virNetworkXMLOptionPtr xmlopt);

int
virNetworkObjLoadAllState(virNetworkObjListPtr nets,
                          const char *stateDir,
                          virNetworkXMLOptionPtr xmlopt);

int
virNetworkObjDeleteConfig(const char *configDir,
                          const char *autostartDir,
                          virNetworkObjPtr net);

bool
virNetworkObjBridgeInUse(virNetworkObjListPtr nets,
                         const char *bridge,
                         const char *skipname);

int
virNetworkObjUpdate(virNetworkObjPtr obj,
                    unsigned int command, /* virNetworkUpdateCommand */
                    unsigned int section, /* virNetworkUpdateSection */
                    int parentIndex,
                    const char *xml,
                    virNetworkXMLOptionPtr xmlopt,
                    unsigned int flags);  /* virNetworkUpdateFlags */

int
virNetworkObjListExport(virConnectPtr conn,
                        virNetworkObjListPtr netobjs,
                        virNetworkPtr **nets,
                        virNetworkObjListFilter filter,
                        unsigned int flags);

typedef int
(*virNetworkObjListIterator)(virNetworkObjPtr net,
                             void *opaque);

int
virNetworkObjListForEach(virNetworkObjListPtr nets,
                         virNetworkObjListIterator callback,
                         void *opaque);

int
virNetworkObjListGetNames(virNetworkObjListPtr nets,
                          bool active,
                          char **names,
                          int maxnames,
                          virNetworkObjListFilter filter,
                          virConnectPtr conn);

int
virNetworkObjListNumOfNetworks(virNetworkObjListPtr nets,
                               bool active,
                               virNetworkObjListFilter filter,
                               virConnectPtr conn);

void
virNetworkObjListPrune(virNetworkObjListPtr nets,
                       unsigned int flags);
