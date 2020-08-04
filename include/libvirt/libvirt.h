/* -*- c -*-
 * libvirt.h: Core interfaces for the libvirt library
 * Summary: core interfaces for the libvirt library
 * Description: Provides the interfaces of the libvirt library to handle
 *              virtualized domains
 *
 * Copyright (C) 2005-2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef LIBVIRT_H
# define LIBVIRT_H

# include <sys/types.h>

# ifdef __cplusplus
extern "C" {
# endif

# define __VIR_LIBVIRT_H_INCLUDES__
# include <libvirt/libvirt-common.h>
# include <libvirt/libvirt-host.h>
# include <libvirt/libvirt-domain.h>
# include <libvirt/libvirt-domain-checkpoint.h>
# include <libvirt/libvirt-domain-snapshot.h>
# include <libvirt/libvirt-event.h>
# include <libvirt/libvirt-interface.h>
# include <libvirt/libvirt-network.h>
# include <libvirt/libvirt-nodedev.h>
# include <libvirt/libvirt-nwfilter.h>
# include <libvirt/libvirt-secret.h>
# include <libvirt/libvirt-storage.h>
# include <libvirt/libvirt-stream.h>
# undef __VIR_LIBVIRT_H_INCLUDES__

# ifdef __cplusplus
}
# endif

#endif /* LIBVIRT_H */
