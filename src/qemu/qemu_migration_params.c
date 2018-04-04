/*
 * qemu_migration_params.c: QEMU migration parameters handling
 *
 * Copyright (C) 2006-2018 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 */

#include <config.h>

#include "virlog.h"
#include "virerror.h"
#include "viralloc.h"
#include "virstring.h"

#include "qemu_alias.h"
#include "qemu_hotplug.h"
#include "qemu_migration.h"
#include "qemu_migration_params.h"
#include "qemu_monitor.h"

#define VIR_FROM_THIS VIR_FROM_QEMU

VIR_LOG_INIT("qemu.qemu_migration_params");

#define QEMU_MIGRATION_TLS_ALIAS_BASE "libvirt_migrate"

typedef enum {
    QEMU_MIGRATION_PARAM_TYPE_INT,
    QEMU_MIGRATION_PARAM_TYPE_ULL,
    QEMU_MIGRATION_PARAM_TYPE_BOOL,
    QEMU_MIGRATION_PARAM_TYPE_STRING,
} qemuMigrationParamType;

typedef struct _qemuMigrationParamValue qemuMigrationParamValue;
typedef qemuMigrationParamValue *qemuMigrationParamValuePtr;
struct _qemuMigrationParamValue {
    bool set;
    union {
        int i; /* exempt from syntax-check */
        unsigned long long ull;
        bool b;
        char *s;
    } value;
};

struct _qemuMigrationParams {
    unsigned long long compMethods; /* bit-wise OR of qemuMigrationCompressMethod */
    virBitmapPtr caps;
    qemuMigrationParamValue params[QEMU_MIGRATION_PARAM_LAST];
};

typedef enum {
    QEMU_MIGRATION_COMPRESS_XBZRLE = 0,
    QEMU_MIGRATION_COMPRESS_MT,

    QEMU_MIGRATION_COMPRESS_LAST
} qemuMigrationCompressMethod;
VIR_ENUM_DECL(qemuMigrationCompressMethod)
VIR_ENUM_IMPL(qemuMigrationCompressMethod, QEMU_MIGRATION_COMPRESS_LAST,
              "xbzrle",
              "mt",
);

VIR_ENUM_DECL(qemuMigrationParam)
VIR_ENUM_IMPL(qemuMigrationParam, QEMU_MIGRATION_PARAM_LAST,
              "compress-level",
              "compress-threads",
              "decompress-threads",
              "cpu-throttle-initial",
              "cpu-throttle-increment",
              "tls-creds",
              "tls-hostname",
              "max-bandwidth",
              "downtime-limit",
              "block-incremental",
              "xbzrle-cache-size");

typedef struct _qemuMigrationParamsAlwaysOnItem qemuMigrationParamsAlwaysOnItem;
struct _qemuMigrationParamsAlwaysOnItem {
    qemuMonitorMigrationCaps cap;
    int party; /* bit-wise OR of qemuMigrationParty */
};

typedef struct _qemuMigrationParamsFlagMapItem qemuMigrationParamsFlagMapItem;
struct _qemuMigrationParamsFlagMapItem {
    virDomainMigrateFlags flag;
    qemuMonitorMigrationCaps cap;
    int party; /* bit-wise OR of qemuMigrationParty */
};

/* Migration capabilities which should always be enabled as long as they
 * are supported by QEMU. */
static const qemuMigrationParamsAlwaysOnItem qemuMigrationParamsAlwaysOn[] = {
    {QEMU_MONITOR_MIGRATION_CAPS_PAUSE_BEFORE_SWITCHOVER,
     QEMU_MIGRATION_SOURCE},
};

/* Translation from virDomainMigrateFlags to qemuMonitorMigrationCaps. */
static const qemuMigrationParamsFlagMapItem qemuMigrationParamsFlagMap[] = {
    {VIR_MIGRATE_RDMA_PIN_ALL,
     QEMU_MONITOR_MIGRATION_CAPS_RDMA_PIN_ALL,
     QEMU_MIGRATION_SOURCE | QEMU_MIGRATION_DESTINATION},

    {VIR_MIGRATE_AUTO_CONVERGE,
     QEMU_MONITOR_MIGRATION_CAPS_AUTO_CONVERGE,
     QEMU_MIGRATION_SOURCE},

    {VIR_MIGRATE_POSTCOPY,
     QEMU_MONITOR_MIGRATION_CAPS_POSTCOPY,
     QEMU_MIGRATION_SOURCE | QEMU_MIGRATION_DESTINATION},
};

static const qemuMigrationParamType qemuMigrationParamTypes[] = {
    [QEMU_MIGRATION_PARAM_COMPRESS_LEVEL] = QEMU_MIGRATION_PARAM_TYPE_INT,
    [QEMU_MIGRATION_PARAM_COMPRESS_THREADS] = QEMU_MIGRATION_PARAM_TYPE_INT,
    [QEMU_MIGRATION_PARAM_DECOMPRESS_THREADS] = QEMU_MIGRATION_PARAM_TYPE_INT,
    [QEMU_MIGRATION_PARAM_THROTTLE_INITIAL] = QEMU_MIGRATION_PARAM_TYPE_INT,
    [QEMU_MIGRATION_PARAM_THROTTLE_INCREMENT] = QEMU_MIGRATION_PARAM_TYPE_INT,
    [QEMU_MIGRATION_PARAM_TLS_CREDS] = QEMU_MIGRATION_PARAM_TYPE_STRING,
    [QEMU_MIGRATION_PARAM_TLS_HOSTNAME] = QEMU_MIGRATION_PARAM_TYPE_STRING,
    [QEMU_MIGRATION_PARAM_MAX_BANDWIDTH] = QEMU_MIGRATION_PARAM_TYPE_ULL,
    [QEMU_MIGRATION_PARAM_DOWNTIME_LIMIT] = QEMU_MIGRATION_PARAM_TYPE_ULL,
    [QEMU_MIGRATION_PARAM_BLOCK_INCREMENTAL] = QEMU_MIGRATION_PARAM_TYPE_BOOL,
    [QEMU_MIGRATION_PARAM_XBZRLE_CACHE_SIZE] = QEMU_MIGRATION_PARAM_TYPE_ULL,
};
verify(ARRAY_CARDINALITY(qemuMigrationParamTypes) == QEMU_MIGRATION_PARAM_LAST);


static qemuMigrationParamsPtr
qemuMigrationParamsNew(void)
{
    qemuMigrationParamsPtr params;

    if (VIR_ALLOC(params) < 0)
        return NULL;

    params->caps = virBitmapNew(QEMU_MONITOR_MIGRATION_CAPS_LAST);
    if (!params->caps)
        goto error;

    return params;

 error:
    qemuMigrationParamsFree(params);
    return NULL;
}


void
qemuMigrationParamsFree(qemuMigrationParamsPtr migParams)
{
    size_t i;

    if (!migParams)
        return;

    for (i = 0; i < QEMU_MIGRATION_PARAM_LAST; i++) {
        if (qemuMigrationParamTypes[i] == QEMU_MIGRATION_PARAM_TYPE_STRING)
            VIR_FREE(migParams->params[i].value.s);
    }

    virBitmapFree(migParams->caps);
    VIR_FREE(migParams);
}


static int
qemuMigrationParamsCheckType(qemuMigrationParam param,
                             qemuMigrationParamType type)
{
    if (qemuMigrationParamTypes[param] != type) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Type mismatch for '%s' migration parameter"),
                       qemuMigrationParamTypeToString(param));
        return -1;
    }

    return 0;
}


static int
qemuMigrationParamsGetTPInt(qemuMigrationParamsPtr migParams,
                            qemuMigrationParam param,
                            virTypedParameterPtr params,
                            int nparams,
                            const char *name)
{
    int rc;

    if (qemuMigrationParamsCheckType(param, QEMU_MIGRATION_PARAM_TYPE_INT) < 0)
        return -1;

    if (!params)
        return 0;

    if ((rc = virTypedParamsGetInt(params, nparams, name,
                                   &migParams->params[param].value.i)) < 0)
        return -1;

    migParams->params[param].set = !!rc;
    return 0;
}


static int
qemuMigrationParamsSetTPInt(qemuMigrationParamsPtr migParams,
                            qemuMigrationParam param,
                            virTypedParameterPtr *params,
                            int *nparams,
                            int *maxparams,
                            const char *name)
{
    if (qemuMigrationParamsCheckType(param, QEMU_MIGRATION_PARAM_TYPE_INT) < 0)
        return -1;

    if (!migParams->params[param].set)
        return 0;

    return virTypedParamsAddInt(params, nparams, maxparams, name,
                                migParams->params[param].value.i);
}


static int
qemuMigrationParamsGetTPULL(qemuMigrationParamsPtr migParams,
                            qemuMigrationParam param,
                            virTypedParameterPtr params,
                            int nparams,
                            const char *name)
{
    int rc;

    if (qemuMigrationParamsCheckType(param, QEMU_MIGRATION_PARAM_TYPE_ULL) < 0)
        return -1;

    if (!params)
        return 0;

    if ((rc = virTypedParamsGetULLong(params, nparams, name,
                                      &migParams->params[param].value.ull)) < 0)
        return -1;

    migParams->params[param].set = !!rc;
    return 0;
}


static int
qemuMigrationParamsSetTPULL(qemuMigrationParamsPtr migParams,
                            qemuMigrationParam param,
                            virTypedParameterPtr *params,
                            int *nparams,
                            int *maxparams,
                            const char *name)
{
    if (qemuMigrationParamsCheckType(param, QEMU_MIGRATION_PARAM_TYPE_ULL) < 0)
        return -1;

    if (!migParams->params[param].set)
        return 0;

    return virTypedParamsAddULLong(params, nparams, maxparams, name,
                                   migParams->params[param].value.ull);
}


static int
qemuMigrationParamsSetCompression(virTypedParameterPtr params,
                                  int nparams,
                                  unsigned long flags,
                                  qemuMigrationParamsPtr migParams)
{
    size_t i;
    int method;
    qemuMonitorMigrationCaps cap;

    for (i = 0; i < nparams; i++) {
        if (STRNEQ(params[i].field, VIR_MIGRATE_PARAM_COMPRESSION))
            continue;

        method = qemuMigrationCompressMethodTypeFromString(params[i].value.s);
        if (method < 0) {
            virReportError(VIR_ERR_INVALID_ARG,
                           _("Unsupported compression method '%s'"),
                           params[i].value.s);
            goto error;
        }

        if (migParams->compMethods & (1ULL << method)) {
            virReportError(VIR_ERR_INVALID_ARG,
                           _("Compression method '%s' is specified twice"),
                           params[i].value.s);
            goto error;
        }

        migParams->compMethods |= 1ULL << method;

        switch ((qemuMigrationCompressMethod) method) {
        case QEMU_MIGRATION_COMPRESS_XBZRLE:
            cap = QEMU_MONITOR_MIGRATION_CAPS_XBZRLE;
            break;

        case QEMU_MIGRATION_COMPRESS_MT:
            cap = QEMU_MONITOR_MIGRATION_CAPS_COMPRESS;
            break;

        case QEMU_MIGRATION_COMPRESS_LAST:
        default:
            continue;
        }
        ignore_value(virBitmapSetBit(migParams->caps, cap));
    }

    if (qemuMigrationParamsGetTPInt(migParams,
                                    QEMU_MIGRATION_PARAM_COMPRESS_LEVEL,
                                    params, nparams,
                                    VIR_MIGRATE_PARAM_COMPRESSION_MT_LEVEL) < 0)
        goto error;

    if (qemuMigrationParamsGetTPInt(migParams,
                                    QEMU_MIGRATION_PARAM_COMPRESS_THREADS,
                                    params, nparams,
                                    VIR_MIGRATE_PARAM_COMPRESSION_MT_THREADS) < 0)
        goto error;

    if (qemuMigrationParamsGetTPInt(migParams,
                                    QEMU_MIGRATION_PARAM_DECOMPRESS_THREADS,
                                    params, nparams,
                                    VIR_MIGRATE_PARAM_COMPRESSION_MT_DTHREADS) < 0)
        goto error;

    if (qemuMigrationParamsGetTPULL(migParams,
                                    QEMU_MIGRATION_PARAM_XBZRLE_CACHE_SIZE,
                                    params, nparams,
                                    VIR_MIGRATE_PARAM_COMPRESSION_XBZRLE_CACHE) < 0)
        goto error;

    if ((migParams->params[QEMU_MIGRATION_PARAM_COMPRESS_LEVEL].set ||
         migParams->params[QEMU_MIGRATION_PARAM_COMPRESS_THREADS].set ||
         migParams->params[QEMU_MIGRATION_PARAM_DECOMPRESS_THREADS].set) &&
        !(migParams->compMethods & (1ULL << QEMU_MIGRATION_COMPRESS_MT))) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("Turn multithread compression on to tune it"));
        goto error;
    }

    if (migParams->params[QEMU_MIGRATION_PARAM_XBZRLE_CACHE_SIZE].set &&
        !(migParams->compMethods & (1ULL << QEMU_MIGRATION_COMPRESS_XBZRLE))) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("Turn xbzrle compression on to tune it"));
        goto error;
    }

    if (!migParams->compMethods && (flags & VIR_MIGRATE_COMPRESSED)) {
        migParams->compMethods = 1ULL << QEMU_MIGRATION_COMPRESS_XBZRLE;
        ignore_value(virBitmapSetBit(migParams->caps,
                                     QEMU_MONITOR_MIGRATION_CAPS_XBZRLE));
    }

    return 0;

 error:
    return -1;
}


qemuMigrationParamsPtr
qemuMigrationParamsFromFlags(virTypedParameterPtr params,
                             int nparams,
                             unsigned long flags,
                             qemuMigrationParty party)
{
    qemuMigrationParamsPtr migParams;
    size_t i;

    if (!(migParams = qemuMigrationParamsNew()))
        return NULL;

    for (i = 0; i < ARRAY_CARDINALITY(qemuMigrationParamsFlagMap); i++) {
        if (qemuMigrationParamsFlagMap[i].party & party &&
            flags & qemuMigrationParamsFlagMap[i].flag) {
            ignore_value(virBitmapSetBit(migParams->caps,
                                         qemuMigrationParamsFlagMap[i].cap));
        }
    }

    if (party == QEMU_MIGRATION_SOURCE) {
        if (qemuMigrationParamsGetTPInt(migParams,
                                        QEMU_MIGRATION_PARAM_THROTTLE_INITIAL,
                                        params, nparams,
                                        VIR_MIGRATE_PARAM_AUTO_CONVERGE_INITIAL) < 0)
            goto error;

        if (qemuMigrationParamsGetTPInt(migParams,
                                        QEMU_MIGRATION_PARAM_THROTTLE_INCREMENT,
                                        params, nparams,
                                        VIR_MIGRATE_PARAM_AUTO_CONVERGE_INCREMENT) < 0)
            goto error;
    }

    if ((migParams->params[QEMU_MIGRATION_PARAM_THROTTLE_INITIAL].set ||
         migParams->params[QEMU_MIGRATION_PARAM_THROTTLE_INCREMENT].set) &&
        !(flags & VIR_MIGRATE_AUTO_CONVERGE)) {
        virReportError(VIR_ERR_INVALID_ARG, "%s",
                       _("Turn auto convergence on to tune it"));
        goto error;
    }

    if (qemuMigrationParamsSetCompression(params, nparams, flags, migParams) < 0)
        goto error;

    return migParams;

 error:
    qemuMigrationParamsFree(migParams);
    return NULL;
}


int
qemuMigrationParamsDump(qemuMigrationParamsPtr migParams,
                        virTypedParameterPtr *params,
                        int *nparams,
                        int *maxparams,
                        unsigned long *flags)
{
    size_t i;

    if (migParams->compMethods == 1ULL << QEMU_MIGRATION_COMPRESS_XBZRLE &&
        !migParams->params[QEMU_MIGRATION_PARAM_XBZRLE_CACHE_SIZE].set) {
        *flags |= VIR_MIGRATE_COMPRESSED;
        return 0;
    }

    for (i = 0; i < QEMU_MIGRATION_COMPRESS_LAST; ++i) {
        if ((migParams->compMethods & (1ULL << i)) &&
            virTypedParamsAddString(params, nparams, maxparams,
                                    VIR_MIGRATE_PARAM_COMPRESSION,
                                    qemuMigrationCompressMethodTypeToString(i)) < 0)
            return -1;
    }

    if (qemuMigrationParamsSetTPInt(migParams,
                                    QEMU_MIGRATION_PARAM_COMPRESS_LEVEL,
                                    params, nparams, maxparams,
                                    VIR_MIGRATE_PARAM_COMPRESSION_MT_LEVEL) < 0)
        return -1;

    if (qemuMigrationParamsSetTPInt(migParams,
                                    QEMU_MIGRATION_PARAM_COMPRESS_THREADS,
                                    params, nparams, maxparams,
                                    VIR_MIGRATE_PARAM_COMPRESSION_MT_THREADS) < 0)
        return -1;

    if (qemuMigrationParamsSetTPInt(migParams,
                                    QEMU_MIGRATION_PARAM_DECOMPRESS_THREADS,
                                    params, nparams, maxparams,
                                    VIR_MIGRATE_PARAM_COMPRESSION_MT_DTHREADS) < 0)
        return -1;

    if (qemuMigrationParamsSetTPULL(migParams,
                                    QEMU_MIGRATION_PARAM_XBZRLE_CACHE_SIZE,
                                    params, nparams, maxparams,
                                    VIR_MIGRATE_PARAM_COMPRESSION_XBZRLE_CACHE) < 0)
        return -1;

    return 0;
}


static qemuMigrationParamsPtr
qemuMigrationParamsFromJSON(virJSONValuePtr params)
{
    qemuMigrationParamsPtr migParams;
    qemuMigrationParamValuePtr pv;
    const char *name;
    const char *str;
    size_t i;

    if (!(migParams = qemuMigrationParamsNew()))
        return NULL;

    if (!params)
        return migParams;

    for (i = 0; i < QEMU_MIGRATION_PARAM_LAST; i++) {
        name = qemuMigrationParamTypeList[i];
        pv = &migParams->params[i];

        switch (qemuMigrationParamTypes[i]) {
        case QEMU_MIGRATION_PARAM_TYPE_INT:
            if (virJSONValueObjectGetNumberInt(params, name, &pv->value.i) == 0)
                pv->set = true;
            break;

        case QEMU_MIGRATION_PARAM_TYPE_ULL:
            if (virJSONValueObjectGetNumberUlong(params, name, &pv->value.ull) == 0)
                pv->set = true;
            break;

        case QEMU_MIGRATION_PARAM_TYPE_BOOL:
            if (virJSONValueObjectGetBoolean(params, name, &pv->value.b) == 0)
                pv->set = true;
            break;

        case QEMU_MIGRATION_PARAM_TYPE_STRING:
            if ((str = virJSONValueObjectGetString(params, name))) {
                if (VIR_STRDUP(pv->value.s, str) < 0)
                    goto error;
                pv->set = true;
            }
            break;
        }
    }

    return migParams;

 error:
    qemuMigrationParamsFree(migParams);
    return NULL;
}


static virJSONValuePtr
qemuMigrationParamsToJSON(qemuMigrationParamsPtr migParams)
{
    virJSONValuePtr params = NULL;
    qemuMigrationParamValuePtr pv;
    const char *name;
    size_t i;
    int rc;

    if (!(params = virJSONValueNewObject()))
        return NULL;

    for (i = 0; i < QEMU_MIGRATION_PARAM_LAST; i++) {
        name = qemuMigrationParamTypeList[i];
        pv = &migParams->params[i];

        if (!pv->set)
            continue;

        rc = 0;
        switch (qemuMigrationParamTypes[i]) {
        case QEMU_MIGRATION_PARAM_TYPE_INT:
            rc = virJSONValueObjectAppendNumberInt(params, name, pv->value.i);
            break;

        case QEMU_MIGRATION_PARAM_TYPE_ULL:
            rc = virJSONValueObjectAppendNumberUlong(params, name, pv->value.ull);
            break;

        case QEMU_MIGRATION_PARAM_TYPE_BOOL:
            rc = virJSONValueObjectAppendBoolean(params, name, pv->value.b);
            break;

        case QEMU_MIGRATION_PARAM_TYPE_STRING:
            rc = virJSONValueObjectAppendString(params, name, pv->value.s);
            break;
        }

        if (rc < 0)
            goto error;
    }

    return params;

 error:
    virJSONValueFree(params);
    return NULL;
}


/**
 * qemuMigrationParamsApply
 * @driver: qemu driver
 * @vm: domain object
 * @asyncJob: migration job
 * @migParams: migration parameters to send to QEMU
 *
 * Send all parameters stored in @migParams to QEMU.
 *
 * Returns 0 on success, -1 on failure.
 */
int
qemuMigrationParamsApply(virQEMUDriverPtr driver,
                         virDomainObjPtr vm,
                         int asyncJob,
                         qemuMigrationParamsPtr migParams)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    bool xbzrleCacheSize_old = false;
    virJSONValuePtr params = NULL;
    qemuMigrationParam xbzrle = QEMU_MIGRATION_PARAM_XBZRLE_CACHE_SIZE;
    int ret = -1;

    if (qemuDomainObjEnterMonitorAsync(driver, vm, asyncJob) < 0)
        return -1;

    if (qemuMonitorSetMigrationCapabilities(priv->mon, priv->migrationCaps,
                                            migParams->caps) < 0)
        goto cleanup;

    /* If QEMU is too old to support xbzrle-cache-size migration parameter,
     * we need to set it via migrate-set-cache-size and tell
     * qemuMonitorSetMigrationParams to ignore this parameter.
     */
    if (migParams->params[xbzrle].set &&
        (!priv->job.migParams ||
         !priv->job.migParams->params[xbzrle].set)) {
        if (qemuMonitorSetMigrationCacheSize(priv->mon,
                                             migParams->params[xbzrle].value.ull) < 0)
            goto cleanup;
        xbzrleCacheSize_old = true;
        migParams->params[xbzrle].set = false;
    }

    if (!(params = qemuMigrationParamsToJSON(migParams)))
        goto cleanup;

    if (virJSONValueObjectKeysNumber(params) == 0) {
        ret = 0;
        goto cleanup;
    }

    ret = qemuMonitorSetMigrationParams(priv->mon, params);
    params = NULL;

 cleanup:
    if (qemuDomainObjExitMonitor(driver, vm) < 0)
        ret = -1;

    if (xbzrleCacheSize_old)
        migParams->params[xbzrle].set = true;

    virJSONValueFree(params);

    return ret;
}


/* qemuMigrationParamsEnableTLS
 * @driver: pointer to qemu driver
 * @vm: domain object
 * @tlsListen: server or client
 * @asyncJob: Migration job to join
 * @tlsAlias: alias to be generated for TLS object
 * @secAlias: alias to be generated for a secinfo object
 * @hostname: hostname of the migration destination
 * @migParams: migration parameters to set
 *
 * Create the TLS objects for the migration and set the migParams value.
 * If QEMU itself does not connect to the destination @hostname must be
 * provided for certificate verification.
 *
 * Returns 0 on success, -1 on failure
 */
int
qemuMigrationParamsEnableTLS(virQEMUDriverPtr driver,
                             virDomainObjPtr vm,
                             bool tlsListen,
                             int asyncJob,
                             char **tlsAlias,
                             char **secAlias,
                             const char *hostname,
                             qemuMigrationParamsPtr migParams)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    virJSONValuePtr tlsProps = NULL;
    virJSONValuePtr secProps = NULL;
    virQEMUDriverConfigPtr cfg = virQEMUDriverGetConfig(driver);
    int ret = -1;

    if (!cfg->migrateTLSx509certdir) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                       _("host migration TLS directory not configured"));
        goto error;
    }

    if (!priv->job.migParams->params[QEMU_MIGRATION_PARAM_TLS_CREDS].set) {
        virReportError(VIR_ERR_OPERATION_UNSUPPORTED, "%s",
                       _("TLS migration is not supported with this "
                         "QEMU binary"));
        goto error;
    }

    /* If there's a secret, then grab/store it now using the connection */
    if (cfg->migrateTLSx509secretUUID &&
        !(priv->migSecinfo =
          qemuDomainSecretInfoTLSNew(priv, QEMU_MIGRATION_TLS_ALIAS_BASE,
                                     cfg->migrateTLSx509secretUUID)))
        goto error;

    if (qemuDomainGetTLSObjects(priv->qemuCaps, priv->migSecinfo,
                                cfg->migrateTLSx509certdir, tlsListen,
                                cfg->migrateTLSx509verify,
                                QEMU_MIGRATION_TLS_ALIAS_BASE,
                                &tlsProps, tlsAlias, &secProps, secAlias) < 0)
        goto error;

    /* Ensure the domain doesn't already have the TLS objects defined...
     * This should prevent any issues just in case some cleanup wasn't
     * properly completed (both src and dst use the same alias) or
     * some other error path between now and perform . */
    qemuDomainDelTLSObjects(driver, vm, asyncJob, *secAlias, *tlsAlias);

    if (qemuDomainAddTLSObjects(driver, vm, asyncJob, *secAlias, &secProps,
                                *tlsAlias, &tlsProps) < 0)
        goto error;

    if (qemuMigrationParamsSetString(migParams,
                                     QEMU_MIGRATION_PARAM_TLS_CREDS,
                                     *tlsAlias) < 0 ||
        qemuMigrationParamsSetString(migParams,
                                     QEMU_MIGRATION_PARAM_TLS_HOSTNAME,
                                     hostname ? hostname : "") < 0)
        goto error;

    ret = 0;

 cleanup:
    virObjectUnref(cfg);
    return ret;

 error:
    virJSONValueFree(tlsProps);
    virJSONValueFree(secProps);
    goto cleanup;
}


/* qemuMigrationParamsDisableTLS
 * @vm: domain object
 * @migParams: Pointer to a migration parameters block
 *
 * If we support setting the tls-creds, then set both tls-creds and
 * tls-hostname to the empty string ("") which indicates to not use
 * TLS on this migration.
 *
 * Returns 0 on success, -1 on failure
 */
int
qemuMigrationParamsDisableTLS(virDomainObjPtr vm,
                              qemuMigrationParamsPtr migParams)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;

    if (!priv->job.migParams->params[QEMU_MIGRATION_PARAM_TLS_CREDS].set)
        return 0;

    if (qemuMigrationParamsSetString(migParams,
                                     QEMU_MIGRATION_PARAM_TLS_CREDS, "") < 0 ||
        qemuMigrationParamsSetString(migParams,
                                     QEMU_MIGRATION_PARAM_TLS_HOSTNAME, "") < 0)
        return -1;

    return 0;
}


/* qemuMigrationParamsResetTLS
 * @driver: pointer to qemu driver
 * @vm: domain object
 * @asyncJob: migration job to join
 *
 * Deconstruct all the setup possibly done for TLS - delete the TLS and
 * security objects, free the secinfo, and reset the migration params to "".
 */
static void
qemuMigrationParamsResetTLS(virQEMUDriverPtr driver,
                            virDomainObjPtr vm,
                            int asyncJob,
                            qemuMigrationParamsPtr origParams)
{
    char *tlsAlias = NULL;
    char *secAlias = NULL;

    /* If QEMU does not support TLS migration we didn't set the aliases. */
    if (!origParams->params[QEMU_MIGRATION_PARAM_TLS_CREDS].set)
        return;

    /* NB: If either or both fail to allocate memory we can still proceed
     *     since the next time we migrate another deletion attempt will be
     *     made after successfully generating the aliases. */
    tlsAlias = qemuAliasTLSObjFromSrcAlias(QEMU_MIGRATION_TLS_ALIAS_BASE);
    secAlias = qemuDomainGetSecretAESAlias(QEMU_MIGRATION_TLS_ALIAS_BASE, false);

    qemuDomainDelTLSObjects(driver, vm, asyncJob, secAlias, tlsAlias);
    qemuDomainSecretInfoFree(&QEMU_DOMAIN_PRIVATE(vm)->migSecinfo);

    VIR_FREE(tlsAlias);
    VIR_FREE(secAlias);
}


int
qemuMigrationParamsFetch(virQEMUDriverPtr driver,
                         virDomainObjPtr vm,
                         int asyncJob,
                         qemuMigrationParamsPtr *migParams)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    virJSONValuePtr jsonParams = NULL;
    int ret = -1;
    int rc;

    *migParams = NULL;

    if (qemuDomainObjEnterMonitorAsync(driver, vm, asyncJob) < 0)
        goto cleanup;

    rc = qemuMonitorGetMigrationParams(priv->mon, &jsonParams);

    if (qemuDomainObjExitMonitor(driver, vm) < 0 || rc < 0)
        goto cleanup;

    if (!(*migParams = qemuMigrationParamsFromJSON(jsonParams)))
        goto cleanup;

    ret = 0;

 cleanup:
    virJSONValueFree(jsonParams);
    return ret;
}


int
qemuMigrationParamsSetString(qemuMigrationParamsPtr migParams,
                             qemuMigrationParam param,
                             const char *value)
{
    if (qemuMigrationParamsCheckType(param, QEMU_MIGRATION_PARAM_TYPE_STRING) < 0)
        return -1;

    if (VIR_STRDUP(migParams->params[param].value.s, value) < 0)
        return -1;

    return 0;
}


/**
 * Returns  0 on success,
 *          1 if the parameter is not supported by QEMU.
 */
int
qemuMigrationParamsGetDowntimeLimit(qemuMigrationParamsPtr migParams,
                                    unsigned long long *value)
{
    if (!migParams->params[QEMU_MIGRATION_PARAM_DOWNTIME_LIMIT].set)
        return 1;

    *value = migParams->params[QEMU_MIGRATION_PARAM_DOWNTIME_LIMIT].value.ull;
    return 0;
}


/**
 * qemuMigrationParamsCheck:
 *
 * Check supported migration parameters and keep their original values in
 * qemuDomainJobObj so that we can properly reset them at the end of migration.
 * Reports an error if any of the currently used capabilities in @migParams
 * are unsupported by QEMU.
 */
int
qemuMigrationParamsCheck(virQEMUDriverPtr driver,
                         virDomainObjPtr vm,
                         int asyncJob,
                         qemuMigrationParamsPtr migParams)
{
    qemuDomainObjPrivatePtr priv = vm->privateData;
    qemuMonitorMigrationCaps cap;
    qemuMigrationParty party;
    size_t i;

    if (asyncJob == QEMU_ASYNC_JOB_MIGRATION_OUT)
        party = QEMU_MIGRATION_SOURCE;
    else
        party = QEMU_MIGRATION_DESTINATION;

    for (cap = 0; cap < QEMU_MONITOR_MIGRATION_CAPS_LAST; cap++) {
        bool state = false;

        ignore_value(virBitmapGetBit(migParams->caps, cap, &state));

        if (state && !qemuMigrationCapsGet(vm, cap)) {
            virReportError(VIR_ERR_ARGUMENT_UNSUPPORTED,
                           _("Migration option '%s' is not supported by QEMU binary"),
                           qemuMonitorMigrationCapsTypeToString(cap));
            return -1;
        }
    }

    for (i = 0; i < ARRAY_CARDINALITY(qemuMigrationParamsAlwaysOn); i++) {
        if (qemuMigrationParamsAlwaysOn[i].party & party &&
            qemuMigrationCapsGet(vm, qemuMigrationParamsAlwaysOn[i].cap)) {
            ignore_value(virBitmapSetBit(migParams->caps,
                                         qemuMigrationParamsAlwaysOn[i].cap));
        }
    }

    /*
     * We want to disable all migration capabilities after migration, no need
     * to ask QEMU for their current settings.
     */

    return qemuMigrationParamsFetch(driver, vm, asyncJob, &priv->job.migParams);
}


/*
 * qemuMigrationParamsReset:
 *
 * Reset all migration parameters so that the next job which internally uses
 * migration (save, managedsave, snapshots, dump) will not try to use them.
 */
void
qemuMigrationParamsReset(virQEMUDriverPtr driver,
                         virDomainObjPtr vm,
                         int asyncJob,
                         qemuMigrationParamsPtr origParams)
{
    virErrorPtr err = virSaveLastError();

    VIR_DEBUG("Resetting migration parameters %p", origParams);

    if (!virDomainObjIsActive(vm) || !origParams)
        goto cleanup;

    if (qemuMigrationParamsApply(driver, vm, asyncJob, origParams) < 0)
        goto cleanup;

    qemuMigrationParamsResetTLS(driver, vm, asyncJob, origParams);

 cleanup:
    if (err) {
        virSetError(err);
        virFreeError(err);
    }
}
