/*
 * virfirewalldpriv.h: private APIs for firewalld
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef LIBVIRT_VIRFIREWALLDPRIV_H_ALLOW
# error "virfirewalldpriv.h may only be included by virfirewalld.c or test suites"
#endif /* LIBVIRT_VIRFIREWALLDPRIV_H_ALLOW */

#pragma once

#define VIR_FIREWALL_FIREWALLD_SERVICE "org.fedoraproject.FirewallD1"
