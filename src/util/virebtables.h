/*
 * virebtables.h: Helper APIs for managing ebtables
 *
 * Copyright (C) 2007-2008, 2013 Red Hat, Inc.
 * Copyright (C) 2009 IBM Corp.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virmacaddr.h"

typedef struct _ebtablesContext ebtablesContext;

ebtablesContext *ebtablesContextNew              (const char *driver);
void             ebtablesContextFree             (ebtablesContext *ctx);

int              ebtablesAddForwardAllowIn       (ebtablesContext *ctx,
                                                  const char *iface,
                                                  const virMacAddr *mac);
int              ebtablesRemoveForwardAllowIn    (ebtablesContext *ctx,
                                                  const char *iface,
                                                  const virMacAddr *mac);

int              ebtablesAddForwardPolicyReject(ebtablesContext *ctx);
