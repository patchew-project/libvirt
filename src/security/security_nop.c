/*
 * Copyright (C) 2010-2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <config.h>

#include "security_nop.h"
#include "virstring.h"
#include "virerror.h"

#define VIR_FROM_THIS VIR_FROM_SECURITY

static virSecurityDriverStatus
virSecurityDriverProbeNop(const char *virtDriver G_GNUC_UNUSED)
{
    return SECURITY_DRIVER_ENABLE;
}

static int
virSecurityDriverOpenNop(virSecurityManagerPtr mgr G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDriverCloseNop(virSecurityManagerPtr mgr G_GNUC_UNUSED)
{
    return 0;
}

static const char *
virSecurityDriverGetModelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED)
{
    return "none";
}

static const char *
virSecurityDriverGetDOINop(virSecurityManagerPtr mgr G_GNUC_UNUSED)
{
    return "0";
}

static int
virSecurityDomainSetDaemonSocketLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                         virDomainDefPtr vm G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainSetSocketLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                   virDomainDefPtr vm G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainClearSocketLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                     virDomainDefPtr vm G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainRestoreHostdevLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                        virDomainDefPtr vm G_GNUC_UNUSED,
                                        virDomainHostdevDefPtr dev G_GNUC_UNUSED,
                                        const char *vroot G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainSetHostdevLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                    virDomainDefPtr vm G_GNUC_UNUSED,
                                    virDomainHostdevDefPtr dev G_GNUC_UNUSED,
                                    const char *vroot G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainGenLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                             virDomainDefPtr sec G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainReserveLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                 virDomainDefPtr sec G_GNUC_UNUSED,
                                 pid_t pid G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainReleaseLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                 virDomainDefPtr sec G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainSetAllLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                virDomainDefPtr sec G_GNUC_UNUSED,
                                const char *incomingPath G_GNUC_UNUSED,
                                bool chardevStdioLogd G_GNUC_UNUSED,
                                bool migrated G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainRestoreAllLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                    virDomainDefPtr vm G_GNUC_UNUSED,
                                    bool migrated G_GNUC_UNUSED,
                                    bool chardevStdioLogd G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainGetProcessLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                    virDomainDefPtr vm G_GNUC_UNUSED,
                                    pid_t pid G_GNUC_UNUSED,
                                    virSecurityLabelPtr sec G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainSetProcessLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                    virDomainDefPtr vm G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainSetChildProcessLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                         virDomainDefPtr vm G_GNUC_UNUSED,
                                         virCommandPtr cmd G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainVerifyNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                           virDomainDefPtr def G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainSetFDLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                               virDomainDefPtr sec G_GNUC_UNUSED,
                               int fd G_GNUC_UNUSED)
{
    return 0;
}

static char *
virSecurityDomainGetMountOptionsNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                    virDomainDefPtr vm G_GNUC_UNUSED)
{
    char *opts;

    opts = g_strdup("");
    return opts;
}

static const char *
virSecurityGetBaseLabel(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                        int virtType G_GNUC_UNUSED)
{
    return NULL;
}

static int
virSecurityDomainRestoreImageLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                      virDomainDefPtr def G_GNUC_UNUSED,
                                      virStorageSourcePtr src G_GNUC_UNUSED,
                                      virSecurityDomainImageLabelFlags flags G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainSetImageLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                  virDomainDefPtr def G_GNUC_UNUSED,
                                  virStorageSourcePtr src G_GNUC_UNUSED,
                                  virSecurityDomainImageLabelFlags flags G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainMoveImageMetadataNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                      pid_t pid G_GNUC_UNUSED,
                                      virStorageSourcePtr src G_GNUC_UNUSED,
                                      virStorageSourcePtr dst G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainSetMemoryLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                   virDomainDefPtr def G_GNUC_UNUSED,
                                   virDomainMemoryDefPtr mem G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainRestoreMemoryLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                       virDomainDefPtr def G_GNUC_UNUSED,
                                       virDomainMemoryDefPtr mem G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainInputLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                               virDomainDefPtr def G_GNUC_UNUSED,
                               virDomainInputDefPtr input G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainSetChardevLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                    virDomainDefPtr def G_GNUC_UNUSED,
                                    virDomainChrSourceDefPtr dev_source G_GNUC_UNUSED,
                                    bool chardevStdioLogd G_GNUC_UNUSED)
{
    return 0;
}

static int
virSecurityDomainRestoreChardevLabelNop(virSecurityManagerPtr mgr G_GNUC_UNUSED,
                                        virDomainDefPtr def G_GNUC_UNUSED,
                                        virDomainChrSourceDefPtr dev_source G_GNUC_UNUSED,
                                        bool chardevStdioLogd G_GNUC_UNUSED)
{
    return 0;
}

virSecurityDriver virSecurityDriverNop = {
    .privateDataLen                     = 0,
    .name                               = "none",
    .probe                              = virSecurityDriverProbeNop,
    .open                               = virSecurityDriverOpenNop,
    .close                              = virSecurityDriverCloseNop,

    .getModel                           = virSecurityDriverGetModelNop,
    .getDOI                             = virSecurityDriverGetDOINop,

    .domainSecurityVerify               = virSecurityDomainVerifyNop,

    .domainSetSecurityImageLabel        = virSecurityDomainSetImageLabelNop,
    .domainRestoreSecurityImageLabel    = virSecurityDomainRestoreImageLabelNop,
    .domainMoveImageMetadata            = virSecurityDomainMoveImageMetadataNop,

    .domainSetSecurityMemoryLabel       = virSecurityDomainSetMemoryLabelNop,
    .domainRestoreSecurityMemoryLabel   = virSecurityDomainRestoreMemoryLabelNop,

    .domainSetSecurityInputLabel        = virSecurityDomainInputLabelNop,
    .domainRestoreSecurityInputLabel    = virSecurityDomainInputLabelNop,

    .domainSetSecurityDaemonSocketLabel = virSecurityDomainSetDaemonSocketLabelNop,
    .domainSetSecuritySocketLabel       = virSecurityDomainSetSocketLabelNop,
    .domainClearSecuritySocketLabel     = virSecurityDomainClearSocketLabelNop,

    .domainGenSecurityLabel             = virSecurityDomainGenLabelNop,
    .domainReserveSecurityLabel         = virSecurityDomainReserveLabelNop,
    .domainReleaseSecurityLabel         = virSecurityDomainReleaseLabelNop,

    .domainGetSecurityProcessLabel      = virSecurityDomainGetProcessLabelNop,
    .domainSetSecurityProcessLabel      = virSecurityDomainSetProcessLabelNop,
    .domainSetSecurityChildProcessLabel = virSecurityDomainSetChildProcessLabelNop,

    .domainSetSecurityAllLabel          = virSecurityDomainSetAllLabelNop,
    .domainRestoreSecurityAllLabel      = virSecurityDomainRestoreAllLabelNop,

    .domainSetSecurityHostdevLabel      = virSecurityDomainSetHostdevLabelNop,
    .domainRestoreSecurityHostdevLabel  = virSecurityDomainRestoreHostdevLabelNop,

    .domainSetSecurityImageFDLabel      = virSecurityDomainSetFDLabelNop,
    .domainSetSecurityTapFDLabel        = virSecurityDomainSetFDLabelNop,

    .domainGetSecurityMountOptions      = virSecurityDomainGetMountOptionsNop,

    .getBaseLabel                       = virSecurityGetBaseLabel,

    .domainSetSecurityChardevLabel      = virSecurityDomainSetChardevLabelNop,
    .domainRestoreSecurityChardevLabel  = virSecurityDomainRestoreChardevLabelNop,
};
