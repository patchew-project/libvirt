/*
 * virtpm_conf.c: vTPM XML processing
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

#include <config.h>

#include "virenum.h"
#include "virtpm_conf.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_ENUM_IMPL(virTPMSwtpmFeature,
              VIR_TPM_SWTPM_FEATURE_LAST,
              "cmdarg-pwd-fd",
);

VIR_ENUM_IMPL(virTPMSwtpmSetupFeature,
              VIR_TPM_SWTPM_SETUP_FEATURE_LAST,
              "cmdarg-pwdfile-fd",
);
