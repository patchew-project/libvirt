/*
 * moment_conf.c: domain snapshot/checkpoint base class
 *
 * Copyright (C) 2006-2019 Red Hat, Inc.
 * Copyright (C) 2006-2008 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include <sys/time.h>

#include "internal.h"
#include "moment_conf.h"
#include "domain_conf.h"
#include "virlog.h"
#include "viralloc.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_DOMAIN

VIR_LOG_INIT("conf.moment_conf");

static virClassPtr virDomainMomentDefClass;
static void virDomainMomentDefDispose(void *obj);

static int
virDomainMomentOnceInit(void)
{
    if (!VIR_CLASS_NEW(virDomainMomentDef, virClassForObject()))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(virDomainMoment);

virClassPtr
virClassForDomainMomentDef(void)
{
    if (virDomainMomentInitialize() < 0)
        return NULL;

    return virDomainMomentDefClass;
}

static void
virDomainMomentDefDispose(void *obj)
{
    virDomainMomentDefPtr def = obj;

    VIR_FREE(def->name);
    VIR_FREE(def->description);
    VIR_FREE(def->parent_name);
    virDomainDefFree(def->dom);
    virDomainDefFree(def->inactiveDom);
}

/* Provide defaults for creation time and moment name after parsing XML */
int
virDomainMomentDefPostParse(virDomainMomentDefPtr def)
{
    def->creationTime = g_get_real_time() / (1000*1000);

    if (!def->name)
        def->name = g_strdup_printf("%lld", def->creationTime);

    return 0;
}
