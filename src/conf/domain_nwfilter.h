/*
 * domain_nwfilter.h:
 *
 * Copyright (C) 2010 IBM Corporation
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

int virDomainConfNWFilterInstantiate(const char *vmname,
                                     const unsigned char *vmuuid,
                                     virDomainNetDefPtr net,
                                     bool ignoreExists);
void virDomainConfNWFilterTeardown(virDomainNetDefPtr net);
void virDomainConfVMNWFilterTeardown(virDomainObjPtr vm);
