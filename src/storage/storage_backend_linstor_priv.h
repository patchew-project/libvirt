/*
 * storage_backend_linstor_priv.h: header for functions necessary in tests
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

#ifndef LIBVIRT_STORAGE_BACKEND_LINSTOR_PRIV_H_ALLOW
# error "storage_backend_linstor_priv.h may only be included by storage_backend_linstor.c or test suites"
#endif /* LIBVIRT_STORAGE_BACKEND_LINSTOR_PRIV_H_ALLOW */

#pragma once

#include "virjson.h"
#include "virstorageobj.h"
#include "conf/storage_conf.h"

int
virStorageBackendLinstorFilterRscDefsForRscGroup(const char *resourceGroup,
                                                 const char *output,
                                                 virJSONValuePtr rscDefArrayOut);

int
virStorageBackendLinstorParseResourceGroupList(const char *resourceGroup,
                                               const char *output,
                                               virJSONValuePtr *storPoolArrayOut);

int
virStorageBackendLinstorParseStoragePoolList(virStoragePoolDefPtr pool,
                                             const char* nodeName,
                                             const char *output);

int
virStorageBackendLinstorParseResourceList(virStoragePoolObjPtr pool,
                                          const char* nodeName,
                                          virJSONValuePtr rscDefFilterArr,
                                          const char *outputRscList,
                                          const char *outputVolDef);

int
virStorageBackendLinstorParseVolumeDefinition(virStorageVolDefPtr vol,
                                              const char *output);
