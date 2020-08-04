/*
 * xenxs_private.h: Private definitions for Xen parsing
 *
 * Copyright (C) 2007, 2010, 2012 Red Hat, Inc.
 * Copyright (C) 2011 Univention GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

#include <xen/xen.h>

/* xen-unstable changeset 19788 removed MAX_VIRT_CPUS from public
 * headers.  Its semantic was retained with XEN_LEGACY_MAX_VCPUS.
 * Ensure MAX_VIRT_CPUS is defined accordingly.
 */
#if !defined(MAX_VIRT_CPUS) && defined(XEN_LEGACY_MAX_VCPUS)
# define MAX_VIRT_CPUS XEN_LEGACY_MAX_VCPUS
#endif

#define MIN_XEN_GUEST_SIZE 64  /* 64 megabytes */

#ifdef __sun
# define DEFAULT_VIF_SCRIPT "vif-vnic"
#else
# define DEFAULT_VIF_SCRIPT "vif-bridge"
#endif
