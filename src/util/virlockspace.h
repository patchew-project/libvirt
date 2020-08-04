/*
 * virlockspace.h: simple file based lockspaces
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "virjson.h"

typedef struct _virLockSpace virLockSpace;
typedef virLockSpace *virLockSpacePtr;

virLockSpacePtr virLockSpaceNew(const char *directory);
virLockSpacePtr virLockSpaceNewPostExecRestart(virJSONValuePtr object);

virJSONValuePtr virLockSpacePreExecRestart(virLockSpacePtr lockspace);

void virLockSpaceFree(virLockSpacePtr lockspace);

const char *virLockSpaceGetDirectory(virLockSpacePtr lockspace);

int virLockSpaceCreateResource(virLockSpacePtr lockspace,
                               const char *resname);
int virLockSpaceDeleteResource(virLockSpacePtr lockspace,
                               const char *resname);

typedef enum {
    VIR_LOCK_SPACE_ACQUIRE_SHARED     = (1 << 0),
    VIR_LOCK_SPACE_ACQUIRE_AUTOCREATE = (1 << 1),
} virLockSpaceAcquireFlags;

int virLockSpaceAcquireResource(virLockSpacePtr lockspace,
                                const char *resname,
                                pid_t owner,
                                unsigned int flags);

int virLockSpaceReleaseResource(virLockSpacePtr lockspace,
                                const char *resname,
                                pid_t owner);

int virLockSpaceReleaseResourcesForOwner(virLockSpacePtr lockspace,
                                         pid_t owner);
