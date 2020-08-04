/*
 * vircgroupbackend.c: methods for cgroups backend
 *
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <config.h>

#include "vircgroupbackend.h"
#define LIBVIRT_VIRCGROUPPRIV_H_ALLOW
#include "vircgrouppriv.h"
#include "vircgroupv1.h"
#include "vircgroupv2.h"
#include "virerror.h"
#include "virthread.h"

#define VIR_FROM_THIS VIR_FROM_CGROUP

VIR_ENUM_DECL(virCgroupBackend);
VIR_ENUM_IMPL(virCgroupBackend,
              VIR_CGROUP_BACKEND_TYPE_LAST,
              "cgroup V2",
              "cgroup V1",
);

static virOnceControl virCgroupBackendOnce = VIR_ONCE_CONTROL_INITIALIZER;
static virCgroupBackendPtr virCgroupBackends[VIR_CGROUP_BACKEND_TYPE_LAST] = { 0 };

void
virCgroupBackendRegister(virCgroupBackendPtr backend)
{
    if (virCgroupBackends[backend->type]) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Cgroup backend '%s' already registered."),
                       virCgroupBackendTypeToString(backend->type));
        return;
    }

    virCgroupBackends[backend->type] = backend;
}


static void
virCgroupBackendOnceInit(void)
{
    virCgroupV2Register();
    virCgroupV1Register();
}


virCgroupBackendPtr *
virCgroupBackendGetAll(void)
{
    if (virOnce(&virCgroupBackendOnce, virCgroupBackendOnceInit) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Failed to initialize cgroup backend."));
        return NULL;
    }
    return virCgroupBackends;
}


virCgroupBackendPtr
virCgroupBackendForController(virCgroupPtr group,
                              unsigned int controller)
{
    size_t i;

    for (i = 0; i < VIR_CGROUP_BACKEND_TYPE_LAST; i++) {
        if (group->backends[i] &&
            group->backends[i]->hasController(group, controller)) {
            return group->backends[i];
        }
    }

    return NULL;
}
