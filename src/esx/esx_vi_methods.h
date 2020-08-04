/*
 * esx_vi_methods.h: client for the VMware VI API 2.5 to manage ESX hosts
 *
 * Copyright (C) 2009, 2010 Matthias Bolte <matthias.bolte@googlemail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "esx_vi.h"
#include "esx_vi_types.h"



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * VI Methods
 */

int esxVI_RetrieveServiceContent
      (esxVI_Context *ctx,
       esxVI_ServiceContent **serviceContent);             /* required */

int esxVI_ValidateMigration
      (esxVI_Context *ctx,
       esxVI_ManagedObjectReference *vm,                   /* required, list */
       esxVI_VirtualMachinePowerState state,               /* optional */
       esxVI_String *testType,                             /* optional, list */
       esxVI_ManagedObjectReference *pool,                 /* optional */
       esxVI_ManagedObjectReference *host,                 /* optional */
       esxVI_Event **output);                              /* optional, list */

#include "esx_vi_methods.generated.h"
