/*
 * virtpm_conf.h: vTPM XML processing
 *
 * Copyright (C) 2019 IBM Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once

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
