/* -*- c -*-
 * libvirt-lxc.h: Interfaces specific for LXC driver
 * Summary: lxc specific interfaces
 * Description: Provides the interfaces of the libvirt library to handle
 *              LXC specific methods
 *
 * Copyright (C) 2012-2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef LIBVIRT_LXC_H
# define LIBVIRT_LXC_H

# include <libvirt/libvirt.h>

# ifdef __cplusplus
extern "C" {
# endif

int virDomainLxcOpenNamespace(virDomainPtr domain,
                              int **fdlist,
                              unsigned int flags);

int virDomainLxcEnterNamespace(virDomainPtr domain,
                               unsigned int nfdlist,
                               int *fdlist,
                               unsigned int *noldfdlist,
                               int **oldfdlist,
                               unsigned int flags);
int virDomainLxcEnterSecurityLabel(virSecurityModelPtr model,
                                   virSecurityLabelPtr label,
                                   virSecurityLabelPtr oldlabel,
                                   unsigned int flags);
int virDomainLxcEnterCGroup(virDomainPtr domain,
                            unsigned int flags);

# ifdef __cplusplus
}
# endif

#endif /* LIBVIRT_LXC_H */
