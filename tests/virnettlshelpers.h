/*
 * Copyright (C) 2011-2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#if !defined WIN32 && HAVE_LIBTASN1_H && LIBGNUTLS_VERSION_NUMBER >= 0x020600

# include <libtasn1.h>

# include "rpc/virnettlscontext.h"

/*
 * This contains parameter about how to generate
 * certificates.
 */
struct testTLSCertReq {
    gnutls_x509_crt_t crt;

    const char *filename;

    /* Identifying information */
    const char *country;
    const char *cn;
    const char *altname1;
    const char *altname2;
    const char *ipaddr1;
    const char *ipaddr2;

    /* Basic constraints */
    bool basicConstraintsEnable;
    bool basicConstraintsCritical;
    bool basicConstraintsIsCA;

    /* Key usage */
    bool keyUsageEnable;
    bool keyUsageCritical;
    int keyUsageValue;

    /* Key purpose (aka Extended key usage) */
    bool keyPurposeEnable;
    bool keyPurposeCritical;
    const char *keyPurposeOID1;
    const char *keyPurposeOID2;

    /* zero for current time, or non-zero for hours from now */
    int start_offset;
    /* zero for 24 hours from now, or non-zero for hours from now */
    int expire_offset;
};

void testTLSGenerateCert(struct testTLSCertReq *req,
                         gnutls_x509_crt_t ca);
void testTLSWriteCertChain(const char *filename,
                           gnutls_x509_crt_t *certs,
                           size_t ncerts);
void testTLSDiscardCert(struct testTLSCertReq *req);

void testTLSInit(const char *keyfile);
void testTLSCleanup(const char *keyfile);

#endif
