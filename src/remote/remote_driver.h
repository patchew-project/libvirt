/*
 * remote_driver.h: driver to provide access to libvirtd running
 *   on a remote machine
 *
 * Copyright (C) 2006-2007, 2010 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "configmake.h"

int remoteRegister (void);

unsigned long remoteVersion(void);

#define LIBVIRTD_LISTEN_ADDR NULL
#define LIBVIRTD_TLS_PORT "16514"
#define LIBVIRTD_TCP_PORT "16509"

/* Defaults for PKI directory. */
#define LIBVIRT_PKI_DIR SYSCONFDIR "/pki"
#define LIBVIRT_CACERT LIBVIRT_PKI_DIR "/CA/cacert.pem"
#define LIBVIRT_CLIENTKEY LIBVIRT_PKI_DIR "/libvirt/private/clientkey.pem"
#define LIBVIRT_CLIENTCERT LIBVIRT_PKI_DIR "/libvirt/clientcert.pem"
#define LIBVIRT_SERVERKEY LIBVIRT_PKI_DIR "/libvirt/private/serverkey.pem"
#define LIBVIRT_SERVERCERT LIBVIRT_PKI_DIR "/libvirt/servercert.pem"
