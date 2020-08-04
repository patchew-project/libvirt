/*
 * remote_daemon.h: daemon data structure definitions
 *
 * Copyright (C) 2006-2018 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#define VIR_ENUM_SENTINELS

#include <rpc/types.h>
#include <rpc/xdr.h>
#include "remote_protocol.h"
#include "lxc_protocol.h"
#include "qemu_protocol.h"
#include "virthread.h"

#if WITH_SASL
# include "virnetsaslcontext.h"
#endif
#include "virnetserverprogram.h"

typedef struct daemonClientStream daemonClientStream;
typedef daemonClientStream *daemonClientStreamPtr;
typedef struct daemonClientPrivate daemonClientPrivate;
typedef daemonClientPrivate *daemonClientPrivatePtr;
typedef struct daemonClientEventCallback daemonClientEventCallback;
typedef daemonClientEventCallback *daemonClientEventCallbackPtr;

/* Stores the per-client connection state */
struct daemonClientPrivate {
    /* Hold while accessing any data except conn */
    virMutex lock;

    daemonClientEventCallbackPtr *domainEventCallbacks;
    size_t ndomainEventCallbacks;
    daemonClientEventCallbackPtr *networkEventCallbacks;
    size_t nnetworkEventCallbacks;
    daemonClientEventCallbackPtr *qemuEventCallbacks;
    size_t nqemuEventCallbacks;
    daemonClientEventCallbackPtr *storageEventCallbacks;
    size_t nstorageEventCallbacks;
    daemonClientEventCallbackPtr *nodeDeviceEventCallbacks;
    size_t nnodeDeviceEventCallbacks;
    daemonClientEventCallbackPtr *secretEventCallbacks;
    size_t nsecretEventCallbacks;
    bool closeRegistered;

#if WITH_SASL
    virNetSASLSessionPtr sasl;
#endif

    /* This is only valid if a remote open call has been made on this
     * connection, otherwise it will be NULL.  Also if remote close is
     * called, it will be set back to NULL if that succeeds.
     */
    virConnectPtr conn;

    /* These secondary drivers may point back to 'conn'
     * in the monolithic daemon setups. Otherwise they
     * can be NULL and opened on first use, pointing
     * to remote driver use of an external daemon
     */
    virConnectPtr interfaceConn;
    const char *interfaceURI;
    virConnectPtr networkConn;
    const char *networkURI;
    virConnectPtr nodedevConn;
    const char *nodedevURI;
    virConnectPtr nwfilterConn;
    const char *nwfilterURI;
    virConnectPtr secretConn;
    const char *secretURI;
    virConnectPtr storageConn;
    const char *storageURI;
    bool readonly;

    daemonClientStreamPtr streams;
};


#if WITH_SASL
extern virNetSASLContextPtr saslCtxt;
#endif
extern virNetServerProgramPtr remoteProgram;
extern virNetServerProgramPtr qemuProgram;
