/*
 * vircommandpriv.h: Functions for testing virCommand APIs
 *
 * Copyright (C) 2014 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#ifndef LIBVIRT_VIRCOMMANDPRIV_H_ALLOW
# error "vircommandpriv.h may only be included by vircommand.c or test suites"
#endif /* LIBVIRT_VIRCOMMANDPRIV_H_ALLOW */

#pragma once

#include "vircommand.h"

typedef void (*virCommandDryRunCallback)(const char *const*args,
                                         const char *const*env,
                                         const char *input,
                                         char **output,
                                         char **error,
                                         int *status,
                                         void *opaque);

void virCommandSetDryRun(virBufferPtr buf,
                         virCommandDryRunCallback cb,
                         void *opaque);
