/*
 * qemu_slirp.h: QEMU Slirp support
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "qemu_conf.h"
#include "virbitmap.h"
#include "virenum.h"

typedef enum {
    QEMU_SLIRP_FEATURE_NONE = 0,
    QEMU_SLIRP_FEATURE_IPV4,
    QEMU_SLIRP_FEATURE_IPV6,
    QEMU_SLIRP_FEATURE_TFTP,
    QEMU_SLIRP_FEATURE_DBUS_ADDRESS,
    QEMU_SLIRP_FEATURE_DBUS_P2P,
    QEMU_SLIRP_FEATURE_MIGRATE,
    QEMU_SLIRP_FEATURE_RESTRICT,
    QEMU_SLIRP_FEATURE_EXIT_WITH_PARENT,

    QEMU_SLIRP_FEATURE_LAST,
} qemuSlirpFeature;

VIR_ENUM_DECL(qemuSlirpFeature);

typedef struct _qemuSlirp qemuSlirp;
typedef qemuSlirp *qemuSlirpPtr;
struct _qemuSlirp {
    int fd[2];
    virBitmapPtr features;
    pid_t pid;
};

qemuSlirpPtr qemuSlirpNew(void);

qemuSlirpPtr qemuSlirpNewForHelper(const char *helper);

void qemuSlirpFree(qemuSlirpPtr slirp);

void qemuSlirpSetFeature(qemuSlirpPtr slirp,
                         qemuSlirpFeature feature);

bool qemuSlirpHasFeature(const qemuSlirp *slirp,
                         qemuSlirpFeature feature);

int qemuSlirpOpen(qemuSlirpPtr slirp,
                  virQEMUDriverPtr driver,
                  virDomainDefPtr def);

int qemuSlirpStart(qemuSlirpPtr slirp,
                   virDomainObjPtr vm,
                   virQEMUDriverPtr driver,
                   virDomainNetDefPtr net,
                   bool incoming);

void qemuSlirpStop(qemuSlirpPtr slirp,
                   virDomainObjPtr vm,
                   virQEMUDriverPtr driver,
                   virDomainNetDefPtr net);

int qemuSlirpGetFD(qemuSlirpPtr slirp);

int qemuSlirpSetupCgroup(qemuSlirpPtr slirp,
                         virCgroupPtr cgroup);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(qemuSlirp, qemuSlirpFree);
