/*
 * domain_parse.h: XML parser for the domain definition
 *
 * Copyright (C) 2006-2019 Red Hat, Inc.
 * Copyright (C) 2006-2008 Daniel P. Berrange
 * Copyright (c) 2015 SUSE LINUX Products GmbH, Nuernberg, Germany.
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
 */

#pragma once

#include "internal.h"
#include "virconftypes.h"
#include "virdomaintypes.h"
#include "virsavecookie.h"

/* Called after everything else has been parsed, for adjusting basics.
 * This has similar semantics to virDomainDefPostParseCallback, but no
 * parseOpaque is used. This callback is run prior to
 * virDomainDefPostParseCallback. */
typedef int (*virDomainDefPostParseBasicCallback)(virDomainDefPtr def,
                                                  virCapsPtr caps,
                                                  void *opaque);

/* Called once after everything else has been parsed, for adjusting
 * overall domain defaults.
 * @parseOpaque is opaque data passed by virDomainDefParse* caller,
 * @opaque is opaque data set by driver (usually pointer to driver
 * private data). Non-fatal failures should be reported by returning 1. In
 * cases when that is allowed, such failure is translated to a success return
 * value and the failure is noted in def->postParseFailed. Drivers should then
 * re-run the post parse callback when attempting to use such definition. */
typedef int (*virDomainDefPostParseCallback)(virDomainDefPtr def,
                                             virCapsPtr caps,
                                             unsigned int parseFlags,
                                             void *opaque,
                                             void *parseOpaque);
/* Called once per device, for adjusting per-device settings while
 * leaving the overall domain otherwise unchanged.
 * @parseOpaque is opaque data passed by virDomainDefParse* caller,
 * @opaque is opaque data set by driver (usually pointer to driver
 * private data). */
typedef int (*virDomainDeviceDefPostParseCallback)(virDomainDeviceDefPtr dev,
                                                   const virDomainDef *def,
                                                   virCapsPtr caps,
                                                   unsigned int parseFlags,
                                                   void *opaque,
                                                   void *parseOpaque);
/* Drive callback for assigning device addresses, called at the end
 * of parsing, after all defaults and implicit devices have been added.
 * @parseOpaque is opaque data passed by virDomainDefParse* caller,
 * @opaque is opaque data set by driver (usually pointer to driver
 * private data). */
typedef int (*virDomainDefAssignAddressesCallback)(virDomainDef *def,
                                                   virCapsPtr caps,
                                                   unsigned int parseFlags,
                                                   void *opaque,
                                                   void *parseOpaque);

typedef int (*virDomainDefPostParseDataAlloc)(const virDomainDef *def,
                                              virCapsPtr caps,
                                              unsigned int parseFlags,
                                              void *opaque,
                                              void **parseOpaque);
typedef void (*virDomainDefPostParseDataFree)(void *parseOpaque);

/* Called in appropriate places where the domain conf parser can return failure
 * for configurations that were previously accepted. This shall not modify the
 * config. */
typedef int (*virDomainDefValidateCallback)(const virDomainDef *def,
                                            virCapsPtr caps,
                                            void *opaque);

/* Called once per device, for adjusting per-device settings while
 * leaving the overall domain otherwise unchanged.  */
typedef int (*virDomainDeviceDefValidateCallback)(const virDomainDeviceDef *dev,
                                                  const virDomainDef *def,
                                                  void *opaque);

struct _virDomainDefParserConfig {
    /* driver domain definition callbacks */
    virDomainDefPostParseBasicCallback domainPostParseBasicCallback;
    virDomainDefPostParseDataAlloc domainPostParseDataAlloc;
    virDomainDefPostParseCallback domainPostParseCallback;
    virDomainDeviceDefPostParseCallback devicesPostParseCallback;
    virDomainDefAssignAddressesCallback assignAddressesCallback;
    virDomainDefPostParseDataFree domainPostParseDataFree;

    /* validation callbacks */
    virDomainDefValidateCallback domainValidateCallback;
    virDomainDeviceDefValidateCallback deviceValidateCallback;

    /* private data for the callbacks */
    void *priv;
    virFreeCallback privFree;

    /* data */
    unsigned int features; /* virDomainDefFeatures */
    unsigned char macPrefix[VIR_MAC_PREFIX_BUFLEN];
};

typedef void *(*virDomainXMLPrivateDataAllocFunc)(void *);
typedef void (*virDomainXMLPrivateDataFreeFunc)(void *);
typedef virObjectPtr (*virDomainXMLPrivateDataNewFunc)(void);
typedef int (*virDomainXMLPrivateDataFormatFunc)(virBufferPtr,
                                                 virDomainObjPtr);
typedef int (*virDomainXMLPrivateDataParseFunc)(xmlXPathContextPtr,
                                                virDomainObjPtr,
                                                virDomainDefParserConfigPtr);

typedef void *(*virDomainXMLPrivateDataGetParseOpaqueFunc)(virDomainObjPtr vm);

typedef int (*virDomainXMLPrivateDataDiskParseFunc)(xmlXPathContextPtr ctxt,
                                                    virDomainDiskDefPtr disk);
typedef int (*virDomainXMLPrivateDataDiskFormatFunc)(virDomainDiskDefPtr disk,
                                                     virBufferPtr buf);

typedef int (*virDomainXMLPrivateDataStorageSourceParseFunc)(xmlXPathContextPtr ctxt,
                                                             virStorageSourcePtr src);
typedef int (*virDomainXMLPrivateDataStorageSourceFormatFunc)(virStorageSourcePtr src,
                                                              virBufferPtr buf);


struct _virDomainXMLPrivateDataCallbacks {
    virDomainXMLPrivateDataAllocFunc  alloc;
    virDomainXMLPrivateDataFreeFunc   free;
    /* note that private data for devices are not copied when using
     * virDomainDefCopy and similar functions */
    virDomainXMLPrivateDataNewFunc    diskNew;
    virDomainXMLPrivateDataDiskParseFunc diskParse;
    virDomainXMLPrivateDataDiskFormatFunc diskFormat;
    virDomainXMLPrivateDataNewFunc    vcpuNew;
    virDomainXMLPrivateDataNewFunc    chrSourceNew;
    virDomainXMLPrivateDataNewFunc    vsockNew;
    virDomainXMLPrivateDataNewFunc    graphicsNew;
    virDomainXMLPrivateDataFormatFunc format;
    virDomainXMLPrivateDataParseFunc  parse;
    /* following function shall return a pointer which will be used as the
     * 'parseOpaque' argument for virDomainDefPostParse */
    virDomainXMLPrivateDataGetParseOpaqueFunc getParseOpaque;
    virDomainXMLPrivateDataStorageSourceParseFunc storageParse;
    virDomainXMLPrivateDataStorageSourceFormatFunc storageFormat;
};

typedef bool (*virDomainABIStabilityDomain)(const virDomainDef *src,
                                            const virDomainDef *dst);

struct _virDomainABIStability {
    virDomainABIStabilityDomain domain;
};

virDomainXMLOptionPtr virDomainXMLOptionNew(virDomainDefParserConfigPtr config,
                                            virDomainXMLPrivateDataCallbacksPtr priv,
                                            virDomainXMLNamespacePtr xmlns,
                                            virDomainABIStabilityPtr abi,
                                            virSaveCookieCallbacksPtr saveCookie);

virSaveCookieCallbacksPtr
virDomainXMLOptionGetSaveCookie(virDomainXMLOptionPtr xmlopt);

typedef int (*virDomainMomentPostParseCallback)(virDomainMomentDefPtr def);

void virDomainXMLOptionSetMomentPostParse(virDomainXMLOptionPtr xmlopt,
                                          virDomainMomentPostParseCallback cb);
int virDomainXMLOptionRunMomentPostParse(virDomainXMLOptionPtr xmlopt,
                                         virDomainMomentDefPtr def);

void virDomainNetGenerateMAC(virDomainXMLOptionPtr xmlopt, virMacAddrPtr mac);

virDomainXMLNamespacePtr
virDomainXMLOptionGetNamespace(virDomainXMLOptionPtr xmlopt)
    ATTRIBUTE_NONNULL(1);

int virDomainDefPostParse(virDomainDefPtr def,
                          virCapsPtr caps,
                          unsigned int parseFlags,
                          virDomainXMLOptionPtr xmlopt,
                          void *parseOpaque);

int virDomainDeviceValidateAliasForHotplug(virDomainObjPtr vm,
                                           virDomainDeviceDefPtr dev,
                                           unsigned int flags);

bool virDomainDeviceAliasIsUserAlias(const char *aliasStr);

int virDomainDefValidate(virDomainDefPtr def,
                         virCapsPtr caps,
                         unsigned int parseFlags,
                         virDomainXMLOptionPtr xmlopt);
typedef enum {
    /* parse internal domain status information */
    VIR_DOMAIN_DEF_PARSE_STATUS          = 1 << 0,
    /* Parse only parts of the XML that would be present in an inactive libvirt
     * XML. Note that the flag does not imply that ABI incompatible
     * transformations can be used, since it's used to strip runtime info when
     * restoring save images/migration. */
    VIR_DOMAIN_DEF_PARSE_INACTIVE        = 1 << 1,
    /* parse <actual> element */
    VIR_DOMAIN_DEF_PARSE_ACTUAL_NET      = 1 << 2,
    /* parse original states of host PCI device */
    VIR_DOMAIN_DEF_PARSE_PCI_ORIG_STATES = 1 << 3,
    /* internal flag passed to device info sub-parser to allow using <rom> */
    VIR_DOMAIN_DEF_PARSE_ALLOW_ROM       = 1 << 4,
    /* internal flag passed to device info sub-parser to allow specifying boot order */
    VIR_DOMAIN_DEF_PARSE_ALLOW_BOOT      = 1 << 5,
    /* parse only source half of <disk> */
    VIR_DOMAIN_DEF_PARSE_DISK_SOURCE     = 1 << 6,
    /* perform RNG schema validation on the passed XML document */
    VIR_DOMAIN_DEF_PARSE_VALIDATE_SCHEMA = 1 << 7,
    /* allow updates in post parse callback that would break ABI otherwise */
    VIR_DOMAIN_DEF_PARSE_ABI_UPDATE = 1 << 8,
    /* skip definition validation checks meant to be executed on define time only */
    VIR_DOMAIN_DEF_PARSE_SKIP_VALIDATE = 1 << 9,
    /* skip parsing of security labels */
    VIR_DOMAIN_DEF_PARSE_SKIP_SECLABEL        = 1 << 10,
    /* Allows updates in post parse callback for incoming persistent migration
     * that would break ABI otherwise.  This should be used only if it's safe
     * to do such change. */
    VIR_DOMAIN_DEF_PARSE_ABI_UPDATE_MIGRATION = 1 << 11,
    /* Allows to ignore certain failures in the post parse callbacks, which
     * may happen due to missing packages and can be fixed by re-running the
     * post parse callbacks before starting. Failure of the post parse callback
     * is recorded as def->postParseFail */
    VIR_DOMAIN_DEF_PARSE_ALLOW_POST_PARSE_FAIL = 1 << 12,
} virDomainDefParseFlags;

/* Use these flags to skip specific domain ABI consistency checks done
 * in virDomainDefCheckABIStabilityFlags.
 */
typedef enum {
    /* Set when domain lock must be released and there exists the possibility
     * that some external action could alter the value, such as cur_balloon. */
    VIR_DOMAIN_DEF_ABI_CHECK_SKIP_VOLATILE = 1 << 0,
} virDomainDefABICheckFlags;

virDomainDeviceDefPtr virDomainDeviceDefParse(const char *xmlStr,
                                              const virDomainDef *def,
                                              virCapsPtr caps,
                                              virDomainXMLOptionPtr xmlopt,
                                              unsigned int flags);
virDomainDiskDefPtr virDomainDiskDefParse(const char *xmlStr,
                                          const virDomainDef *def,
                                          virDomainXMLOptionPtr xmlopt,
                                          unsigned int flags);
virDomainDefPtr virDomainDefParseString(const char *xmlStr,
                                        virCapsPtr caps,
                                        virDomainXMLOptionPtr xmlopt,
                                        void *parseOpaque,
                                        unsigned int flags);
virDomainDefPtr virDomainDefParseFile(const char *filename,
                                      virCapsPtr caps,
                                      virDomainXMLOptionPtr xmlopt,
                                      void *parseOpaque,
                                      unsigned int flags);
virDomainDefPtr virDomainDefParseNode(xmlDocPtr doc,
                                      xmlNodePtr root,
                                      virCapsPtr caps,
                                      virDomainXMLOptionPtr xmlopt,
                                      void *parseOpaque,
                                      unsigned int flags);
virDomainObjPtr virDomainObjParseNode(xmlDocPtr xml,
                                      xmlNodePtr root,
                                      virCapsPtr caps,
                                      virDomainXMLOptionPtr xmlopt,
                                      unsigned int flags);
virDomainObjPtr virDomainObjParseFile(const char *filename,
                                      virCapsPtr caps,
                                      virDomainXMLOptionPtr xmlopt,
                                      unsigned int flags);

bool virDomainDefCheckABIStability(virDomainDefPtr src,
                                   virDomainDefPtr dst,
                                   virDomainXMLOptionPtr xmlopt);

bool virDomainDefCheckABIStabilityFlags(virDomainDefPtr src,
                                        virDomainDefPtr dst,
                                        virDomainXMLOptionPtr xmlopt,
                                        unsigned int flags);

int virDomainStorageNetworkParseHost(xmlNodePtr hostnode,
                                     virStorageNetHostDefPtr host);
int
virDomainParseMemory(const char *xpath,
                     const char *units_xpath,
                     xmlXPathContextPtr ctxt,
                     unsigned long long *mem,
                     bool required,
                     bool capped);

virStorageSourcePtr
virDomainStorageSourceParseBase(const char *type,
                                const char *format,
                                const char *index)
    ATTRIBUTE_RETURN_CHECK;

int virDomainStorageSourceParse(xmlNodePtr node,
                                xmlXPathContextPtr ctxt,
                                virStorageSourcePtr src,
                                unsigned int flags,
                                virDomainXMLOptionPtr xmlopt)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(3);

int
virDomainDiskBackingStoreParse(xmlXPathContextPtr ctxt,
                               virStorageSourcePtr src,
                               unsigned int flags,
                               virDomainXMLOptionPtr xmlopt)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_RETURN_CHECK;
