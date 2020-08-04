/*
 * virsh-console.h: A dumb serial console client
 *
 * Copyright (C) 2007, 2010, 2012-2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#ifndef WIN32

# include <virsh.h>

int virshRunConsole(vshControl *ctl,
                    virDomainPtr dom,
                    const char *dev_name,
                    unsigned int flags);

#endif /* !WIN32 */
