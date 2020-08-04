/*
 * Copyright (C) 2010-2012 Red Hat, Inc.
 * Copyright IBM Corp. 2008
 *
 * lxc_domain.h: LXC domain helpers
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vircgroup.h"
#include "lxc_conf.h"
#include "lxc_monitor.h"
#include "virenum.h"


typedef enum {
    VIR_LXC_DOMAIN_NAMESPACE_SHARENET = 0,
    VIR_LXC_DOMAIN_NAMESPACE_SHAREIPC,
    VIR_LXC_DOMAIN_NAMESPACE_SHAREUTS,
    VIR_LXC_DOMAIN_NAMESPACE_LAST,
} virLXCDomainNamespace;

typedef enum {
    VIR_LXC_DOMAIN_NAMESPACE_SOURCE_NONE,
    VIR_LXC_DOMAIN_NAMESPACE_SOURCE_NAME,
    VIR_LXC_DOMAIN_NAMESPACE_SOURCE_PID,
    VIR_LXC_DOMAIN_NAMESPACE_SOURCE_NETNS,

    VIR_LXC_DOMAIN_NAMESPACE_SOURCE_LAST,
} virLXCDomainNamespaceSource;

VIR_ENUM_DECL(virLXCDomainNamespace);
VIR_ENUM_DECL(virLXCDomainNamespaceSource);

typedef struct _lxcDomainDef lxcDomainDef;
typedef lxcDomainDef *lxcDomainDefPtr;
struct _lxcDomainDef {
    int ns_source[VIR_LXC_DOMAIN_NAMESPACE_LAST]; /* virLXCDomainNamespaceSource */
    char *ns_val[VIR_LXC_DOMAIN_NAMESPACE_LAST];
};


/* Only 1 job is allowed at any time
 * A job includes *all* lxc.so api, even those just querying
 * information, not merely actions */

enum virLXCDomainJob {
    LXC_JOB_NONE = 0,      /* Always set to 0 for easy if (jobActive) conditions */
    LXC_JOB_QUERY,         /* Doesn't change any state */
    LXC_JOB_DESTROY,       /* Destroys the domain (cannot be masked out) */
    LXC_JOB_MODIFY,        /* May change state */
    LXC_JOB_LAST
};
VIR_ENUM_DECL(virLXCDomainJob);


struct virLXCDomainJobObj {
    virCond cond;                       /* Use to coordinate jobs */
    enum virLXCDomainJob active;        /* Currently running job */
    int owner;                          /* Thread which set current job */
};


typedef struct _virLXCDomainObjPrivate virLXCDomainObjPrivate;
typedef virLXCDomainObjPrivate *virLXCDomainObjPrivatePtr;
struct _virLXCDomainObjPrivate {
    virLXCMonitorPtr monitor;
    bool doneStopEvent;
    int stopReason;
    bool wantReboot;

    pid_t initpid;

    virCgroupPtr cgroup;
    char *machineName;

    struct virLXCDomainJobObj job;
};

extern virXMLNamespace virLXCDriverDomainXMLNamespace;
extern virDomainXMLPrivateDataCallbacks virLXCDriverPrivateDataCallbacks;
extern virDomainDefParserConfig virLXCDriverDomainDefParserConfig;

int
virLXCDomainObjBeginJob(virLXCDriverPtr driver,
                       virDomainObjPtr obj,
                       enum virLXCDomainJob job)
    G_GNUC_WARN_UNUSED_RESULT;

void
virLXCDomainObjEndJob(virLXCDriverPtr driver,
                     virDomainObjPtr obj);


char *
virLXCDomainGetMachineName(virDomainDefPtr def, pid_t pid);

int
virLXCDomainSetRunlevel(virDomainObjPtr vm,
                        int runlevel);
