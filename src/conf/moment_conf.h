/*
 * moment_conf.h: domain snapshot/checkpoint base class
 *                  (derived from snapshot_conf.h)
 *
 * Copyright (C) 2006-2019 Red Hat, Inc.
 * Copyright (C) 2006-2008 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virconftypes.h"
#include "virobject.h"

/* Base class for a domain moment */
struct _virDomainMomentDef {
    virObject parent;

    /* Common portion of public XML.  */
    char *name;
    char *description;
    char *parent_name;
    long long creationTime; /* in seconds */

    /*
     * Store the active domain definition in case of online
     * guest and the inactive domain definition in case of
     * offline guest
     */
    virDomainDefPtr dom;

    /*
     * Store the inactive domain definition in case of online
     * guest and leave NULL in case of offline guest
     */
    virDomainDefPtr inactiveDom;
};

virClassPtr virClassForDomainMomentDef(void);

int virDomainMomentDefPostParse(virDomainMomentDefPtr def);
