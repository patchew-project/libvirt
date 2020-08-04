/*
 * virnettlscontext.h: TLS encryption/x509 handling
 *
 * Copyright (C) 2010-2011 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"
#include "virobject.h"

typedef struct _virNetTLSContext virNetTLSContext;
typedef virNetTLSContext *virNetTLSContextPtr;

typedef struct _virNetTLSSession virNetTLSSession;
typedef virNetTLSSession *virNetTLSSessionPtr;


void virNetTLSInit(void);

virNetTLSContextPtr virNetTLSContextNewServerPath(const char *pkipath,
                                                  bool tryUserPkiPath,
                                                  const char *const *x509dnACL,
                                                  const char *priority,
                                                  bool sanityCheckCert,
                                                  bool requireValidCert);

virNetTLSContextPtr virNetTLSContextNewClientPath(const char *pkipath,
                                                  bool tryUserPkiPath,
                                                  const char *priority,
                                                  bool sanityCheckCert,
                                                  bool requireValidCert);

virNetTLSContextPtr virNetTLSContextNewServer(const char *cacert,
                                              const char *cacrl,
                                              const char *cert,
                                              const char *key,
                                              const char *const *x509dnACL,
                                              const char *priority,
                                              bool sanityCheckCert,
                                              bool requireValidCert);

virNetTLSContextPtr virNetTLSContextNewClient(const char *cacert,
                                              const char *cacrl,
                                              const char *cert,
                                              const char *key,
                                              const char *priority,
                                              bool sanityCheckCert,
                                              bool requireValidCert);

int virNetTLSContextReloadForServer(virNetTLSContextPtr ctxt,
                                    bool tryUserPkiPath);

int virNetTLSContextCheckCertificate(virNetTLSContextPtr ctxt,
                                     virNetTLSSessionPtr sess);


typedef ssize_t (*virNetTLSSessionWriteFunc)(const char *buf, size_t len,
                                             void *opaque);
typedef ssize_t (*virNetTLSSessionReadFunc)(char *buf, size_t len,
                                            void *opaque);

virNetTLSSessionPtr virNetTLSSessionNew(virNetTLSContextPtr ctxt,
                                        const char *hostname);

void virNetTLSSessionSetIOCallbacks(virNetTLSSessionPtr sess,
                                    virNetTLSSessionWriteFunc writeFunc,
                                    virNetTLSSessionReadFunc readFunc,
                                    void *opaque);

ssize_t virNetTLSSessionWrite(virNetTLSSessionPtr sess,
                              const char *buf, size_t len);
ssize_t virNetTLSSessionRead(virNetTLSSessionPtr sess,
                             char *buf, size_t len);

int virNetTLSSessionHandshake(virNetTLSSessionPtr sess);

typedef enum {
    VIR_NET_TLS_HANDSHAKE_COMPLETE,
    VIR_NET_TLS_HANDSHAKE_SENDING,
    VIR_NET_TLS_HANDSHAKE_RECVING,
} virNetTLSSessionHandshakeStatus;

virNetTLSSessionHandshakeStatus
virNetTLSSessionGetHandshakeStatus(virNetTLSSessionPtr sess);

int virNetTLSSessionGetKeySize(virNetTLSSessionPtr sess);

const char *virNetTLSSessionGetX509DName(virNetTLSSessionPtr sess);
