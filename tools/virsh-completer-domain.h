/*
 * virsh-completer-domain.h: virsh completer callbacks related to domains
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "vsh.h"

char ** virshDomainNameCompleter(vshControl *ctl,
                                 const vshCmd *cmd,
                                 unsigned int flags);

enum {
    VIRSH_DOMAIN_INTERFACE_COMPLETER_MAC = 1 << 0, /* Return just MACs */
};

char ** virshDomainInterfaceCompleter(vshControl *ctl,
                                      const vshCmd *cmd,
                                      unsigned int flags);

char ** virshDomainDiskTargetCompleter(vshControl *ctl,
                                       const vshCmd *cmd,
                                       unsigned int flags);

char ** virshDomainEventNameCompleter(vshControl *ctl,
                                      const vshCmd *cmd,
                                      unsigned int flags);

char ** virshDomainInterfaceStateCompleter(vshControl *ctl,
                                           const vshCmd *cmd,
                                           unsigned int flags);

char ** virshDomainDeviceAliasCompleter(vshControl *ctl,
                                        const vshCmd *cmd,
                                        unsigned int flags);

char ** virshDomainShutdownModeCompleter(vshControl *ctl,
                                         const vshCmd *cmd,
                                         unsigned int flags);

char **
virshDomainInterfaceAddrSourceCompleter(vshControl *ctl,
                                        const vshCmd *cmd,
                                        unsigned int flags);

char ** virshDomainHostnameSourceCompleter(vshControl *ctl,
                                           const vshCmd *cmd,
                                           unsigned int flags);
