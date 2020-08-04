/*
 * virt-host-validate-common.h: Sanity check helper APIs
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "internal.h"
#include "virbitmap.h"
#include "virenum.h"

typedef enum {
    VIR_HOST_VALIDATE_FAIL,
    VIR_HOST_VALIDATE_WARN,
    VIR_HOST_VALIDATE_NOTE,

    VIR_HOST_VALIDATE_LAST,
} virHostValidateLevel;

typedef enum {
    VIR_HOST_VALIDATE_CPU_FLAG_VMX = 0,
    VIR_HOST_VALIDATE_CPU_FLAG_SVM,
    VIR_HOST_VALIDATE_CPU_FLAG_SIE,
    VIR_HOST_VALIDATE_CPU_FLAG_FACILITY_158,
    VIR_HOST_VALIDATE_CPU_FLAG_SEV,

    VIR_HOST_VALIDATE_CPU_FLAG_LAST,
} virHostValidateCPUFlag;

VIR_ENUM_DECL(virHostValidateCPUFlag);

void virHostMsgSetQuiet(bool quietFlag);

void virHostMsgCheck(const char *prefix,
                     const char *format,
                     ...) G_GNUC_PRINTF(2, 3);

void virHostMsgPass(void);
void virHostMsgFail(virHostValidateLevel level,
                    const char *format,
                    ...) G_GNUC_PRINTF(2, 3);

int virHostValidateDeviceExists(const char *hvname,
                                const char *dev_name,
                                virHostValidateLevel level,
                                const char *hint);

int virHostValidateDeviceAccessible(const char *hvname,
                                    const char *dev_name,
                                    virHostValidateLevel level,
                                    const char *hint);

virBitmapPtr virHostValidateGetCPUFlags(void);

int virHostValidateLinuxKernel(const char *hvname,
                               int version,
                               virHostValidateLevel level,
                               const char *hint);

int virHostValidateNamespace(const char *hvname,
                             const char *ns_name,
                             virHostValidateLevel level,
                             const char *hint);

int virHostValidateCGroupControllers(const char *hvname,
                                     int controllers,
                                     virHostValidateLevel level);

int virHostValidateIOMMU(const char *hvname,
                         virHostValidateLevel level);

int virHostValidateSecureGuests(const char *hvname,
                                virHostValidateLevel level);

bool virHostKernelModuleIsLoaded(const char *module);
