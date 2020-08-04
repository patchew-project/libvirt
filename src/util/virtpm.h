/*
 * virtpm.h: TPM support
 *
 * Copyright (C) 2013 IBM Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

char *virTPMCreateCancelPath(const char *devpath) G_GNUC_NO_INLINE;

char *virTPMGetSwtpm(void);
char *virTPMGetSwtpmSetup(void);
char *virTPMGetSwtpmIoctl(void);
int virTPMEmulatorInit(void);

bool virTPMSwtpmCapsGet(unsigned int cap);
bool virTPMSwtpmSetupCapsGet(unsigned int cap);

typedef enum {
    VIR_TPM_SWTPM_FEATURE_CMDARG_PWD_FD,

    VIR_TPM_SWTPM_FEATURE_LAST
} virTPMSwtpmFeature;

typedef enum {
    VIR_TPM_SWTPM_SETUP_FEATURE_CMDARG_PWDFILE_FD,

    VIR_TPM_SWTPM_SETUP_FEATURE_LAST
} virTPMSwtpmSetupFeature;

VIR_ENUM_DECL(virTPMSwtpmFeature);
VIR_ENUM_DECL(virTPMSwtpmSetupFeature);
