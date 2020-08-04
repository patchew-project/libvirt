/*
 * virsh-domain-monitor.h: Commands to monitor domain status
 *
 * Copyright (C) 2005, 2007-2012 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virsh.h"

char *virshGetDomainDescription(vshControl *ctl, virDomainPtr dom,
                                bool title, unsigned int flags)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;

VIR_ENUM_DECL(virshDomainInterfaceAddressesSource);

extern const vshCmdDef domMonitoringCmds[];
