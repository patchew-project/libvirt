/*
 * domain_nwfilter.c:
 *
 * Copyright (C) 2014 Red Hat, Inc.
 * Copyright (C) 2010 IBM Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include "internal.h"

#include "datatypes.h"
#include "domain_conf.h"
#include "domain_nwfilter.h"
#include "virnwfilterbindingdef.h"
#include "virerror.h"
#include "viralloc.h"
#include "virstring.h"
#include "virlog.h"


VIR_LOG_INIT("conf.domain_nwfilter");

#define VIR_FROM_THIS VIR_FROM_NWFILTER

static virNWFilterBindingDefPtr
virNWFilterBindingDefForNet(const char *vmname,
                            const unsigned char *vmuuid,
                            virDomainNetDefPtr net)
{
    virNWFilterBindingDefPtr ret;

    if (VIR_ALLOC(ret) < 0)
        return NULL;

    ret->ownername = g_strdup(vmname);

    memcpy(ret->owneruuid, vmuuid, sizeof(ret->owneruuid));

    ret->portdevname = g_strdup(net->ifname);

    if (net->type == VIR_DOMAIN_NET_TYPE_DIRECT)
        ret->linkdevname = g_strdup(net->data.direct.linkdev);

    ret->mac = net->mac;

    ret->filter = g_strdup(net->filter);

    if (!(ret->filterparams = virNWFilterHashTableCreate(0)))
        goto error;

    if (net->filterparams &&
        virNWFilterHashTablePutAll(net->filterparams, ret->filterparams) < 0)
        goto error;

    return ret;

 error:
    virNWFilterBindingDefFree(ret);
    return NULL;
}


int
virDomainConfNWFilterInstantiate(const char *vmname,
                                 const unsigned char *vmuuid,
                                 virDomainNetDefPtr net,
                                 bool ignoreExists)
{
    virConnectPtr conn = virGetConnectNWFilter();
    virNWFilterBindingDefPtr def = NULL;
    virNWFilterBindingPtr binding = NULL;
    char *xml = NULL;
    int ret = -1;

    VIR_DEBUG("vmname=%s portdev=%s filter=%s ignoreExists=%d",
              vmname, NULLSTR(net->ifname), NULLSTR(net->filter), ignoreExists);

    if (!conn)
        goto cleanup;

    if (ignoreExists) {
        binding = virNWFilterBindingLookupByPortDev(conn, net->ifname);
        if (binding) {
            ret = 0;
            goto cleanup;
        }
    }

    if (!(def = virNWFilterBindingDefForNet(vmname, vmuuid, net)))
        goto cleanup;

    if (!(xml = virNWFilterBindingDefFormat(def)))
        goto cleanup;

    if (!(binding = virNWFilterBindingCreateXML(conn, xml, 0)))
        goto cleanup;

    ret = 0;

 cleanup:
    VIR_FREE(xml);
    virNWFilterBindingDefFree(def);
    virObjectUnref(binding);
    virObjectUnref(conn);
    return ret;
}


static void
virDomainConfNWFilterTeardownImpl(virConnectPtr conn,
                                  virDomainNetDefPtr net)
{
    virNWFilterBindingPtr binding;

    if (!net->ifname)
        return;

    binding = virNWFilterBindingLookupByPortDev(conn, net->ifname);
    if (!binding)
        return;

    virNWFilterBindingDelete(binding);

    virObjectUnref(binding);
}


void
virDomainConfNWFilterTeardown(virDomainNetDefPtr net)
{
    virConnectPtr conn;

    if (!net->filter)
        return;

    if (!(conn = virGetConnectNWFilter()))
        return;

    virDomainConfNWFilterTeardownImpl(conn, net);

    virObjectUnref(conn);
}

void
virDomainConfVMNWFilterTeardown(virDomainObjPtr vm)
{
    size_t i;
    virConnectPtr conn = NULL;

    for (i = 0; i < vm->def->nnets; i++) {
        virDomainNetDefPtr net = vm->def->nets[i];

        if (!net->filter)
            continue;

        if (!conn && !(conn = virGetConnectNWFilter()))
            return;

        virDomainConfNWFilterTeardownImpl(conn, net);
    }

    virObjectUnref(conn);
}
