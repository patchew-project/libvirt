/*
 * nwfilter_tech_driver.h: network filter technology driver interface
 *
 * Copyright (C) 2006-2014 Red Hat, Inc.
 * Copyright (C) 2006-2008 Daniel P. Berrange
 *
 * Copyright (C) 2010 IBM Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virnwfilterobj.h"

typedef struct _virNWFilterTechDriver virNWFilterTechDriver;
typedef virNWFilterTechDriver *virNWFilterTechDriverPtr;


typedef struct _virNWFilterRuleInst virNWFilterRuleInst;
typedef virNWFilterRuleInst *virNWFilterRuleInstPtr;
struct _virNWFilterRuleInst {
    const char *chainSuffix;
    virNWFilterChainPriority chainPriority;
    virNWFilterRuleDefPtr def;
    virNWFilterRulePriority priority;
    virHashTablePtr vars;
};


typedef int (*virNWFilterTechDrvInit)(bool privileged);
typedef void (*virNWFilterTechDrvShutdown)(void);

typedef int (*virNWFilterRuleApplyNewRules)(const char *ifname,
                                            virNWFilterRuleInstPtr *rules,
                                            size_t nrules);

typedef int (*virNWFilterRuleTeardownNewRules)(const char *ifname);

typedef int (*virNWFilterRuleTeardownOldRules)(const char *ifname);

typedef int (*virNWFilterRuleAllTeardown)(const char *ifname);

typedef int (*virNWFilterCanApplyBasicRules)(void);

typedef int (*virNWFilterApplyBasicRules)(const char *ifname,
                                          const virMacAddr *macaddr);

typedef int (*virNWFilterApplyDHCPOnlyRules)(const char *ifname,
                                             const virMacAddr *macaddr,
                                             virNWFilterVarValuePtr dhcpsrvs,
                                             bool leaveTemporary);

typedef int (*virNWFilterRemoveBasicRules)(const char *ifname);

typedef int (*virNWFilterDropAllRules)(const char *ifname);

enum techDrvFlags {
    TECHDRV_FLAG_INITIALIZED = (1 << 0),
};

struct _virNWFilterTechDriver {
    const char *name;
    enum techDrvFlags flags;

    virNWFilterTechDrvInit init;
    virNWFilterTechDrvShutdown shutdown;

    virNWFilterRuleApplyNewRules applyNewRules;
    virNWFilterRuleTeardownNewRules tearNewRules;
    virNWFilterRuleTeardownOldRules tearOldRules;
    virNWFilterRuleAllTeardown allTeardown;

    virNWFilterCanApplyBasicRules canApplyBasicRules;
    virNWFilterApplyBasicRules applyBasicRules;
    virNWFilterApplyDHCPOnlyRules applyDHCPOnlyRules;
    virNWFilterDropAllRules applyDropAllRules;
    virNWFilterRemoveBasicRules removeBasicRules;
};
