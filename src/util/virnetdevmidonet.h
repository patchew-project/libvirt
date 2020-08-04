/*
 * Copyright (C) 2015 Midokura Sarl.

 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virnetdevvportprofile.h"


int virNetDevMidonetBindPort(const char *ifname,
                             const virNetDevVPortProfile *virtualport)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;

int virNetDevMidonetUnbindPort(const virNetDevVPortProfile *virtualport)
    ATTRIBUTE_NONNULL(1) G_GNUC_WARN_UNUSED_RESULT;
