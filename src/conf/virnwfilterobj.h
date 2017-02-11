/*
 * virnwfilterobj.h: network filter object processing
 *                  (derived from nwfilter_conf.h)
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
#ifndef VIRNWFILTEROBJ_H
# define VIRNWFILTEROBJ_H

# include "internal.h"

# include "nwfilter_conf.h"
# include "virpoolobj.h"

typedef struct _virNWFilterDriverState virNWFilterDriverState;
typedef virNWFilterDriverState *virNWFilterDriverStatePtr;
struct _virNWFilterDriverState {
    virMutex lock;
    bool privileged;

    virPoolObjTablePtr nwfilters;

    char *configDir;
    bool watchingFirewallD;
};


virPoolObjPtr virNWFilterObjAdd(virPoolObjTablePtr nwfilters,
                                virNWFilterDefPtr def);

int virNWFilterObjTestUnassignDef(virPoolObjPtr obj);

int virNWFilterObjNumOfNWFilters(virPoolObjTablePtr nwfilters,
                                 virConnectPtr conn,
                                 virPoolObjACLFilter aclfilter);

int virNWFilterObjGetFilters(virPoolObjTablePtr nwfilters,
                             virConnectPtr conn,
                             virPoolObjACLFilter aclfilter,
                             char **names,
                             int maxnames);

int virNWFilterObjExportList(virConnectPtr conn,
                             virPoolObjTablePtr nwfilters,
                             virNWFilterPtr **filters,
                             virPoolObjACLFilter aclfilter,
                             unsigned int flags);

int virNWFilterObjLoadAllConfigs(virPoolObjTablePtr nwfilters,
                                 const char *configDir);

#endif /* VIRNWFILTEROBJ_H */
