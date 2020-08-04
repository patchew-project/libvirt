/* -*- c -*-
 * lxc_protocol.x: private protocol for communicating between
 *   remote_internal driver and libvirtd.  This protocol is
 *   internal and may change at any time.
 *
 * Copyright (C) 2010-2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

%#include "internal.h"
%#include "remote_protocol.h"

/*----- Protocol. -----*/
struct lxc_domain_open_namespace_args {
    remote_nonnull_domain dom;
    unsigned int flags;
};


/* Define the program number, protocol version and procedure numbers here. */
const LXC_PROGRAM = 0x00068000;
const LXC_PROTOCOL_VERSION = 1;

enum lxc_procedure {
    /* Each function must be preceded by a comment providing one or
     * more annotations:
     *
     * - @generate: none|client|server|both
     *
     *   Whether to generate the dispatch stubs for the server
     *   and/or client code.
     *
     * - @readstream: paramnumber
     * - @writestream: paramnumber
     *
     *   The @readstream or @writestream annotations let daemon and src/remote
     *   create a stream.  The direction is defined from the src/remote point
     *   of view.  A readstream transfers data from daemon to src/remote.  The
     *   <paramnumber> specifies at which offset the stream parameter is inserted
     *   in the function parameter list.
     *
     * - @priority: low|high
     *
     *   Each API that might eventually access hypervisor's monitor (and thus
     *   block) MUST fall into low priority. However, there are some exceptions
     *   to this rule, e.g. domainDestroy. Other APIs MAY be marked as high
     *   priority. If in doubt, it's safe to choose low. Low is taken as default,
     *   and thus can be left out.
     */
    /**
     * @generate: none
     * @priority: low
     * @acl: domain:open_namespace
     */
    LXC_PROC_DOMAIN_OPEN_NAMESPACE = 1
};
