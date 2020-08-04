/*
 * virnwfilterbindingobj.h: network filter binding object processing
 *
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "virnwfilterbindingdef.h"
#include "virobject.h"

typedef struct _virNWFilterBindingObj virNWFilterBindingObj;
typedef virNWFilterBindingObj *virNWFilterBindingObjPtr;

virNWFilterBindingObjPtr
virNWFilterBindingObjNew(void);

virNWFilterBindingDefPtr
virNWFilterBindingObjGetDef(virNWFilterBindingObjPtr obj);

void
virNWFilterBindingObjSetDef(virNWFilterBindingObjPtr obj,
                            virNWFilterBindingDefPtr def);

virNWFilterBindingDefPtr
virNWFilterBindingObjStealDef(virNWFilterBindingObjPtr obj);

bool
virNWFilterBindingObjGetRemoving(virNWFilterBindingObjPtr obj);

void
virNWFilterBindingObjSetRemoving(virNWFilterBindingObjPtr obj,
                                 bool removing);

void
virNWFilterBindingObjEndAPI(virNWFilterBindingObjPtr *obj);

char *
virNWFilterBindingObjConfigFile(const char *dir,
                                const char *name);

int
virNWFilterBindingObjSave(const virNWFilterBindingObj *obj,
                          const char *statusDir);

int
virNWFilterBindingObjDelete(const virNWFilterBindingObj *obj,
                            const char *statusDir);

virNWFilterBindingObjPtr
virNWFilterBindingObjParseFile(const char *filename);

char *
virNWFilterBindingObjFormat(const virNWFilterBindingObj *obj);
