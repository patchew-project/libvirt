/*
 * domain_conf.h: domain XML processing
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

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#include "internal.h"
#include "virconftypes.h"
#include "virdomaintypes.h"
#include "virstorageencryption.h"
#include "cpu_conf.h"
#include "virthread.h"
#include "virsocketaddr.h"
#include "networkcommon_conf.h"
#include "nwfilter_params.h"
#include "numa_conf.h"
#include "virobject.h"
#include "virbitmap.h"
#include "virseclabel.h"
#include "virtypedparam.h"
#include "virsavecookie.h"
#include "virenum.h"

#define IS_USB2_CONTROLLER(ctrl) \
    (((ctrl)->type == VIR_DOMAIN_CONTROLLER_TYPE_USB) && \
     ((ctrl)->model == VIR_DOMAIN_CONTROLLER_MODEL_USB_ICH9_EHCI1 || \
      (ctrl)->model == VIR_DOMAIN_CONTROLLER_MODEL_USB_ICH9_UHCI1 || \
      (ctrl)->model == VIR_DOMAIN_CONTROLLER_MODEL_USB_ICH9_UHCI2 || \
      (ctrl)->model == VIR_DOMAIN_CONTROLLER_MODEL_USB_ICH9_UHCI3))

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

bool
virDomainSCSIDriveAddressIsUsed(const virDomainDef *def,
                                const virDomainDeviceDriveAddress *addr);

int virDomainDefPostParse(virDomainDefPtr def,
                          virCapsPtr caps,
                          unsigned int parseFlags,
                          virDomainXMLOptionPtr xmlopt,
                          void *parseOpaque);
bool virDomainDefHasUSB(const virDomainDef *def);

int virDomainDeviceValidateAliasForHotplug(virDomainObjPtr vm,
                                           virDomainDeviceDefPtr dev,
                                           unsigned int flags);

bool virDomainDeviceAliasIsUserAlias(const char *aliasStr);

int virDomainDefValidate(virDomainDefPtr def,
                         virCapsPtr caps,
                         unsigned int parseFlags,
                         virDomainXMLOptionPtr xmlopt);

static inline bool
virDomainObjIsActive(virDomainObjPtr dom)
{
    return dom->def->id != -1;
}

int virDomainObjCheckActive(virDomainObjPtr dom);

int virDomainDefSetVcpusMax(virDomainDefPtr def,
                            unsigned int vcpus,
                            virDomainXMLOptionPtr xmlopt);
bool virDomainDefHasVcpusOffline(const virDomainDef *def);
unsigned int virDomainDefGetVcpusMax(const virDomainDef *def);
int virDomainDefSetVcpus(virDomainDefPtr def, unsigned int vcpus);
unsigned int virDomainDefGetVcpus(const virDomainDef *def);
virBitmapPtr virDomainDefGetOnlineVcpumap(const virDomainDef *def);
virDomainVcpuDefPtr virDomainDefGetVcpu(virDomainDefPtr def, unsigned int vcpu)
    ATTRIBUTE_RETURN_CHECK;
void virDomainDefVcpuOrderClear(virDomainDefPtr def);
int  virDomainDefGetVcpusTopology(const virDomainDef *def,
                                  unsigned int *maxvcpus);

virDomainObjPtr virDomainObjNew(virDomainXMLOptionPtr caps)
    ATTRIBUTE_NONNULL(1);

void virDomainObjEndAPI(virDomainObjPtr *vm);

bool virDomainObjTaint(virDomainObjPtr obj,
                       virDomainTaintFlags taint);

void virDomainObjBroadcast(virDomainObjPtr vm);
int virDomainObjWait(virDomainObjPtr vm);
int virDomainObjWaitUntil(virDomainObjPtr vm,
                          unsigned long long whenms);

void virDomainPanicDefFree(virDomainPanicDefPtr panic);
void virDomainResourceDefFree(virDomainResourceDefPtr resource);
void virDomainGraphicsDefFree(virDomainGraphicsDefPtr def);
const char *virDomainInputDefGetPath(virDomainInputDefPtr input);
void virDomainInputDefFree(virDomainInputDefPtr def);
virDomainDiskDefPtr virDomainDiskDefNew(virDomainXMLOptionPtr xmlopt);
void virDomainDiskDefFree(virDomainDiskDefPtr def);
void virDomainLeaseDefFree(virDomainLeaseDefPtr def);
int virDomainDiskGetType(virDomainDiskDefPtr def);
void virDomainDiskSetType(virDomainDiskDefPtr def, int type);
const char *virDomainDiskGetSource(virDomainDiskDef const *def);
int virDomainDiskSetSource(virDomainDiskDefPtr def, const char *src)
    ATTRIBUTE_RETURN_CHECK;
void virDomainDiskEmptySource(virDomainDiskDefPtr def);
const char *virDomainDiskGetDriver(const virDomainDiskDef *def);
int virDomainDiskSetDriver(virDomainDiskDefPtr def, const char *name)
    ATTRIBUTE_RETURN_CHECK;
int virDomainDiskGetFormat(virDomainDiskDefPtr def);
void virDomainDiskSetFormat(virDomainDiskDefPtr def, int format);
virDomainControllerDefPtr
virDomainDeviceFindSCSIController(const virDomainDef *def,
                                  virDomainDeviceInfoPtr info);
virDomainDiskDefPtr virDomainDiskFindByBusAndDst(virDomainDefPtr def,
                                                 int bus,
                                                 char *dst);

virDomainControllerDefPtr virDomainControllerDefNew(virDomainControllerType type);
void virDomainControllerDefFree(virDomainControllerDefPtr def);
bool virDomainControllerIsPSeriesPHB(const virDomainControllerDef *cont);

virDomainFSDefPtr virDomainFSDefNew(void);
void virDomainFSDefFree(virDomainFSDefPtr def);
void virDomainActualNetDefFree(virDomainActualNetDefPtr def);
virDomainVsockDefPtr virDomainVsockDefNew(virDomainXMLOptionPtr xmlopt);
void virDomainVsockDefFree(virDomainVsockDefPtr vsock);
void virDomainNetDefClear(virDomainNetDefPtr def);
void virDomainNetDefFree(virDomainNetDefPtr def);
void virDomainSmartcardDefFree(virDomainSmartcardDefPtr def);
void virDomainChrDefFree(virDomainChrDefPtr def);
int virDomainChrSourceDefCopy(virDomainChrSourceDefPtr dest,
                              virDomainChrSourceDefPtr src);
void virDomainSoundCodecDefFree(virDomainSoundCodecDefPtr def);
void virDomainSoundDefFree(virDomainSoundDefPtr def);
void virDomainMemballoonDefFree(virDomainMemballoonDefPtr def);
void virDomainNVRAMDefFree(virDomainNVRAMDefPtr def);
void virDomainWatchdogDefFree(virDomainWatchdogDefPtr def);
virDomainVideoDefPtr virDomainVideoDefNew(void);
void virDomainVideoDefFree(virDomainVideoDefPtr def);
void virDomainVideoDefClear(virDomainVideoDefPtr def);
virDomainHostdevDefPtr virDomainHostdevDefNew(void);
void virDomainHostdevDefClear(virDomainHostdevDefPtr def);
void virDomainHostdevDefFree(virDomainHostdevDefPtr def);
void virDomainHubDefFree(virDomainHubDefPtr def);
void virDomainRedirdevDefFree(virDomainRedirdevDefPtr def);
void virDomainRedirFilterDefFree(virDomainRedirFilterDefPtr def);
void virDomainShmemDefFree(virDomainShmemDefPtr def);
void virDomainDeviceDefFree(virDomainDeviceDefPtr def);
virDomainDeviceDefPtr virDomainDeviceDefCopy(virDomainDeviceDefPtr src,
                                             const virDomainDef *def,
                                             virCapsPtr caps,
                                             virDomainXMLOptionPtr xmlopt);
virDomainDeviceInfoPtr virDomainDeviceGetInfo(virDomainDeviceDefPtr device);
void virDomainDeviceSetData(virDomainDeviceDefPtr device,
                            void *devicedata);
void virDomainTPMDefFree(virDomainTPMDefPtr def);

typedef int (*virDomainDeviceInfoCallback)(virDomainDefPtr def,
                                           virDomainDeviceDefPtr dev,
                                           virDomainDeviceInfoPtr info,
                                           void *opaque);

int virDomainDeviceInfoIterate(virDomainDefPtr def,
                               virDomainDeviceInfoCallback cb,
                               void *opaque);

bool virDomainDefHasDeviceAddress(virDomainDefPtr def,
                                  virDomainDeviceInfoPtr info)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_RETURN_CHECK;

void virDomainDefFree(virDomainDefPtr vm);

virDomainChrSourceDefPtr
virDomainChrSourceDefNew(virDomainXMLOptionPtr xmlopt);

virDomainChrDefPtr virDomainChrDefNew(virDomainXMLOptionPtr xmlopt);

virDomainGraphicsDefPtr
virDomainGraphicsDefNew(virDomainXMLOptionPtr xmlopt);
virDomainDefPtr virDomainDefNew(void);

void virDomainObjAssignDef(virDomainObjPtr domain,
                           virDomainDefPtr def,
                           bool live,
                           virDomainDefPtr *oldDef);
int virDomainObjSetDefTransient(virCapsPtr caps,
                                virDomainXMLOptionPtr xmlopt,
                                virDomainObjPtr domain);
void virDomainObjRemoveTransientDef(virDomainObjPtr domain);
virDomainDefPtr
virDomainObjGetPersistentDef(virCapsPtr caps,
                             virDomainXMLOptionPtr xmlopt,
                             virDomainObjPtr domain);

int virDomainObjUpdateModificationImpact(virDomainObjPtr vm,
                                         unsigned int *flags);

int virDomainObjGetDefs(virDomainObjPtr vm,
                        unsigned int flags,
                        virDomainDefPtr *liveDef,
                        virDomainDefPtr *persDef);
virDomainDefPtr virDomainObjGetOneDefState(virDomainObjPtr vm,
                                           unsigned int flags,
                                           bool *state);
virDomainDefPtr virDomainObjGetOneDef(virDomainObjPtr vm, unsigned int flags);

virDomainDefPtr virDomainDefCopy(virDomainDefPtr src,
                                 virCapsPtr caps,
                                 virDomainXMLOptionPtr xmlopt,
                                 void *parseOpaque,
                                 bool migratable);
virDomainDefPtr virDomainObjCopyPersistentDef(virDomainObjPtr dom,
                                              virCapsPtr caps,
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

typedef enum {
    VIR_DOMAIN_DEF_FORMAT_SECURE          = 1 << 0,
    VIR_DOMAIN_DEF_FORMAT_INACTIVE        = 1 << 1,
    VIR_DOMAIN_DEF_FORMAT_MIGRATABLE      = 1 << 2,
    /* format internal domain status information */
    VIR_DOMAIN_DEF_FORMAT_STATUS          = 1 << 3,
    /* format <actual> element */
    VIR_DOMAIN_DEF_FORMAT_ACTUAL_NET      = 1 << 4,
    /* format original states of host PCI device */
    VIR_DOMAIN_DEF_FORMAT_PCI_ORIG_STATES = 1 << 5,
    VIR_DOMAIN_DEF_FORMAT_ALLOW_ROM       = 1 << 6,
    VIR_DOMAIN_DEF_FORMAT_ALLOW_BOOT      = 1 << 7,
    VIR_DOMAIN_DEF_FORMAT_CLOCK_ADJUST    = 1 << 8,
} virDomainDefFormatFlags;

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

int virDomainDefAddImplicitDevices(virDomainDefPtr def);

virDomainIOThreadIDDefPtr virDomainIOThreadIDFind(const virDomainDef *def,
                                                  unsigned int iothread_id);
virDomainIOThreadIDDefPtr virDomainIOThreadIDAdd(virDomainDefPtr def,
                                                 unsigned int iothread_id);
void virDomainIOThreadIDDel(virDomainDefPtr def, unsigned int iothread_id);

/* When extending this list, remember that libvirt 1.2.12-5.0.0 had a
 * bug that silently ignored unknown flags.  A new flag to add
 * information is okay as long as clients still work when an older
 * server omits the requested output, but a new flag to suppress
 * information could result in a security hole when older libvirt
 * supplies the sensitive information in spite of the flag. */
#define VIR_DOMAIN_XML_COMMON_FLAGS \
    (VIR_DOMAIN_XML_SECURE | VIR_DOMAIN_XML_INACTIVE | \
     VIR_DOMAIN_XML_MIGRATABLE)
unsigned int virDomainDefFormatConvertXMLFlags(unsigned int flags);

char *virDomainDefFormat(virDomainDefPtr def,
                         virCapsPtr caps,
                         unsigned int flags);
char *virDomainObjFormat(virDomainXMLOptionPtr xmlopt,
                         virDomainObjPtr obj,
                         virCapsPtr caps,
                         unsigned int flags);
int virDomainDefFormatInternal(virDomainDefPtr def,
                               virCapsPtr caps,
                               unsigned int flags,
                               virBufferPtr buf,
                               virDomainXMLOptionPtr xmlopt);

int virDomainDiskSourceFormat(virBufferPtr buf,
                              virStorageSourcePtr src,
                              const char *element,
                              int policy,
                              bool attrIndex,
                              unsigned int flags,
                              virDomainXMLOptionPtr xmlopt);

int
virDomainDiskBackingStoreFormat(virBufferPtr buf,
                                virStorageSourcePtr src,
                                virDomainXMLOptionPtr xmlopt,
                                unsigned int flags);

int virDomainNetDefFormat(virBufferPtr buf,
                          virDomainNetDefPtr def,
                          char *prefix,
                          unsigned int flags);

int virDomainDefCompatibleDevice(virDomainDefPtr def,
                                 virDomainDeviceDefPtr dev,
                                 virDomainDeviceDefPtr oldDev,
                                 virDomainDeviceAction action,
                                 bool live);

void virDomainRNGDefFree(virDomainRNGDefPtr def);

int virDomainDiskIndexByAddress(virDomainDefPtr def,
                                virPCIDeviceAddressPtr pci_controller,
                                unsigned int bus, unsigned int target,
                                unsigned int unit);
virDomainDiskDefPtr virDomainDiskByAddress(virDomainDefPtr def,
                                           virPCIDeviceAddressPtr pci_controller,
                                           unsigned int bus,
                                           unsigned int target,
                                           unsigned int unit);
int virDomainDiskIndexByName(virDomainDefPtr def, const char *name,
                             bool allow_ambiguous);
virDomainDiskDefPtr virDomainDiskByName(virDomainDefPtr def,
                                        const char *name,
                                        bool allow_ambiguous);
const char *virDomainDiskPathByName(virDomainDefPtr, const char *name);
int virDomainDiskInsert(virDomainDefPtr def,
                        virDomainDiskDefPtr disk)
    ATTRIBUTE_RETURN_CHECK;
void virDomainDiskInsertPreAlloced(virDomainDefPtr def,
                                   virDomainDiskDefPtr disk);
int virDomainStorageNetworkParseHost(xmlNodePtr hostnode,
                                     virStorageNetHostDefPtr host);
int virDomainDiskDefAssignAddress(virDomainXMLOptionPtr xmlopt,
                                  virDomainDiskDefPtr def,
                                  const virDomainDef *vmdef);

virDomainDiskDefPtr
virDomainDiskRemove(virDomainDefPtr def, size_t i);
virDomainDiskDefPtr
virDomainDiskRemoveByName(virDomainDefPtr def, const char *name);

int virDomainNetFindIdx(virDomainDefPtr def, virDomainNetDefPtr net);
virDomainNetDefPtr virDomainNetFind(virDomainDefPtr def, const char *device);
virDomainNetDefPtr virDomainNetFindByName(virDomainDefPtr def, const char *ifname);
bool virDomainHasNet(virDomainDefPtr def, virDomainNetDefPtr net);
int virDomainNetInsert(virDomainDefPtr def, virDomainNetDefPtr net);
virDomainNetDefPtr virDomainNetRemove(virDomainDefPtr def, size_t i);
void virDomainNetRemoveHostdev(virDomainDefPtr def, virDomainNetDefPtr net);

int virDomainHostdevInsert(virDomainDefPtr def, virDomainHostdevDefPtr hostdev);
virDomainHostdevDefPtr
virDomainHostdevRemove(virDomainDefPtr def, size_t i);
int virDomainHostdevFind(virDomainDefPtr def, virDomainHostdevDefPtr match,
                         virDomainHostdevDefPtr *found);

virDomainGraphicsListenDefPtr
virDomainGraphicsGetListen(virDomainGraphicsDefPtr def, size_t i);
int virDomainGraphicsListenAppendAddress(virDomainGraphicsDefPtr def,
                                         const char *address)
            ATTRIBUTE_NONNULL(1);
int virDomainGraphicsListenAppendSocket(virDomainGraphicsDefPtr def,
                                        const char *socket)
            ATTRIBUTE_NONNULL(1);

virDomainNetType virDomainNetGetActualType(const virDomainNetDef *iface);
const char *virDomainNetGetActualBridgeName(virDomainNetDefPtr iface);
int virDomainNetGetActualBridgeMACTableManager(virDomainNetDefPtr iface);
const char *virDomainNetGetActualDirectDev(virDomainNetDefPtr iface);
int virDomainNetGetActualDirectMode(virDomainNetDefPtr iface);
virDomainHostdevDefPtr virDomainNetGetActualHostdev(virDomainNetDefPtr iface);
virNetDevVPortProfilePtr
virDomainNetGetActualVirtPortProfile(virDomainNetDefPtr iface);
virNetDevBandwidthPtr
virDomainNetGetActualBandwidth(virDomainNetDefPtr iface);
virNetDevVlanPtr virDomainNetGetActualVlan(virDomainNetDefPtr iface);
bool virDomainNetGetActualTrustGuestRxFilters(virDomainNetDefPtr iface);
const char *virDomainNetGetModelString(const virDomainNetDef *net);
int virDomainNetSetModelString(virDomainNetDefPtr et,
                               const char *model);
bool virDomainNetIsVirtioModel(const virDomainNetDef *net);
int virDomainNetAppendIPAddress(virDomainNetDefPtr def,
                                const char *address,
                                int family,
                                unsigned int prefix);

int virDomainControllerInsert(virDomainDefPtr def,
                              virDomainControllerDefPtr controller)
    ATTRIBUTE_RETURN_CHECK;
void virDomainControllerInsertPreAlloced(virDomainDefPtr def,
                                         virDomainControllerDefPtr controller);
int virDomainControllerFind(const virDomainDef *def, int type, int idx);
int virDomainControllerFindByType(virDomainDefPtr def, int type);
int virDomainControllerFindByPCIAddress(virDomainDefPtr def,
                                        virPCIDeviceAddressPtr addr);
int virDomainControllerFindUnusedIndex(virDomainDef const *def, int type);
virDomainControllerDefPtr virDomainControllerRemove(virDomainDefPtr def, size_t i);
const char *virDomainControllerAliasFind(const virDomainDef *def,
                                         int type, int idx)
    ATTRIBUTE_NONNULL(1);

int virDomainLeaseIndex(virDomainDefPtr def,
                        virDomainLeaseDefPtr lease);
int virDomainLeaseInsert(virDomainDefPtr def,
                         virDomainLeaseDefPtr lease);
int virDomainLeaseInsertPreAlloc(virDomainDefPtr def)
    ATTRIBUTE_RETURN_CHECK;
void virDomainLeaseInsertPreAlloced(virDomainDefPtr def,
                                    virDomainLeaseDefPtr lease);
virDomainLeaseDefPtr
virDomainLeaseRemoveAt(virDomainDefPtr def, size_t i);
virDomainLeaseDefPtr
virDomainLeaseRemove(virDomainDefPtr def,
                     virDomainLeaseDefPtr lease);

void
virDomainChrGetDomainPtrs(const virDomainDef *vmdef,
                          virDomainChrDeviceType type,
                          const virDomainChrDef ***arrPtr,
                          size_t *cntPtr)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(3) ATTRIBUTE_NONNULL(4);
virDomainChrDefPtr
virDomainChrFind(virDomainDefPtr def,
                 virDomainChrDefPtr target);
bool
virDomainChrEquals(virDomainChrDefPtr src,
                   virDomainChrDefPtr tgt);
int
virDomainChrPreAlloc(virDomainDefPtr vmdef,
                     virDomainChrDefPtr chr);
void
virDomainChrInsertPreAlloced(virDomainDefPtr vmdef,
                             virDomainChrDefPtr chr);
virDomainChrDefPtr
virDomainChrRemove(virDomainDefPtr vmdef,
                   virDomainChrDefPtr chr);

ssize_t virDomainRNGFind(virDomainDefPtr def, virDomainRNGDefPtr rng);
virDomainRNGDefPtr virDomainRNGRemove(virDomainDefPtr def, size_t idx);

ssize_t virDomainRedirdevDefFind(virDomainDefPtr def,
                                 virDomainRedirdevDefPtr redirdev);
virDomainRedirdevDefPtr virDomainRedirdevDefRemove(virDomainDefPtr def, size_t idx);

int virDomainSaveXML(const char *configDir,
                     virDomainDefPtr def,
                     const char *xml);

int virDomainSaveConfig(const char *configDir,
                        virCapsPtr caps,
                        virDomainDefPtr def);
int virDomainSaveStatus(virDomainXMLOptionPtr xmlopt,
                        const char *statusDir,
                        virDomainObjPtr obj,
                        virCapsPtr caps) ATTRIBUTE_RETURN_CHECK;

typedef void (*virDomainLoadConfigNotify)(virDomainObjPtr dom,
                                          int newDomain,
                                          void *opaque);

int virDomainDeleteConfig(const char *configDir,
                          const char *autostartDir,
                          virDomainObjPtr dom);

char *virDomainConfigFile(const char *dir,
                          const char *name);

int virDiskNameToBusDeviceIndex(virDomainDiskDefPtr disk,
                                int *busIdx,
                                int *devIdx);

virDomainFSDefPtr virDomainGetFilesystemForTarget(virDomainDefPtr def,
                                                  const char *target);
int virDomainFSInsert(virDomainDefPtr def, virDomainFSDefPtr fs);
int virDomainFSIndexByName(virDomainDefPtr def, const char *name);
virDomainFSDefPtr virDomainFSRemove(virDomainDefPtr def, size_t i);

int virDomainVideoDefaultType(const virDomainDef *def);
unsigned int virDomainVideoDefaultRAM(const virDomainDef *def,
                                      const virDomainVideoType type);

typedef int (*virDomainSmartcardDefIterator)(virDomainDefPtr def,
                                             virDomainSmartcardDefPtr dev,
                                             void *opaque);

int virDomainSmartcardDefForeach(virDomainDefPtr def,
                                 bool abortOnError,
                                 virDomainSmartcardDefIterator iter,
                                 void *opaque);

typedef int (*virDomainChrDefIterator)(virDomainDefPtr def,
                                       virDomainChrDefPtr dev,
                                       void *opaque);

int virDomainChrDefForeach(virDomainDefPtr def,
                           bool abortOnError,
                           virDomainChrDefIterator iter,
                           void *opaque);

typedef int (*virDomainDiskDefPathIterator)(virDomainDiskDefPtr disk,
                                            const char *path,
                                            size_t depth,
                                            void *opaque);

typedef int (*virDomainUSBDeviceDefIterator)(virDomainDeviceInfoPtr info,
                                             void *opaque);
int virDomainUSBDeviceDefForeach(virDomainDefPtr def,
                                 virDomainUSBDeviceDefIterator iter,
                                 void *opaque,
                                 bool skipHubs);

int virDomainDiskDefForeachPath(virDomainDiskDefPtr disk,
                                bool ignoreOpenFailure,
                                virDomainDiskDefPathIterator iter,
                                void *opaque);

void
virDomainObjSetState(virDomainObjPtr obj, virDomainState state, int reason)
        ATTRIBUTE_NONNULL(1);
virDomainState
virDomainObjGetState(virDomainObjPtr obj, int *reason)
        ATTRIBUTE_NONNULL(1);

virSecurityLabelDefPtr
virDomainDefGetSecurityLabelDef(virDomainDefPtr def, const char *model);

virSecurityDeviceLabelDefPtr
virDomainChrSourceDefGetSecurityLabelDef(virDomainChrSourceDefPtr def,
                                         const char *model);

typedef const char* (*virEventActionToStringFunc)(int type);
typedef int (*virEventActionFromStringFunc)(const char *type);

int virDomainMemoryInsert(virDomainDefPtr def, virDomainMemoryDefPtr mem)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_RETURN_CHECK;
virDomainMemoryDefPtr virDomainMemoryRemove(virDomainDefPtr def, int idx)
    ATTRIBUTE_NONNULL(1);
int virDomainMemoryFindByDef(virDomainDefPtr def, virDomainMemoryDefPtr mem)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_RETURN_CHECK;
int virDomainMemoryFindInactiveByDef(virDomainDefPtr def,
                                     virDomainMemoryDefPtr mem)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_RETURN_CHECK;

int virDomainShmemDefInsert(virDomainDefPtr def, virDomainShmemDefPtr shmem)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_RETURN_CHECK;
bool virDomainShmemDefEquals(virDomainShmemDefPtr src, virDomainShmemDefPtr dst)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_RETURN_CHECK;
ssize_t virDomainShmemDefFind(virDomainDefPtr def, virDomainShmemDefPtr shmem)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_RETURN_CHECK;
virDomainShmemDefPtr virDomainShmemDefRemove(virDomainDefPtr def, size_t idx)
    ATTRIBUTE_NONNULL(1);
ssize_t virDomainInputDefFind(const virDomainDef *def,
                              const virDomainInputDef *input)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_RETURN_CHECK;
bool virDomainVsockDefEquals(const virDomainVsockDef *a,
                             const virDomainVsockDef *b)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_RETURN_CHECK;

virDomainControllerDefPtr
virDomainDefAddController(virDomainDefPtr def, int type, int idx, int model);
int
virDomainDefAddUSBController(virDomainDefPtr def, int idx, int model);
int
virDomainDefMaybeAddController(virDomainDefPtr def,
                               int type,
                               int idx,
                               int model);
int
virDomainDefMaybeAddInput(virDomainDefPtr def,
                          int type,
                          int bus);

char *virDomainDefGetDefaultEmulator(virDomainDefPtr def, virCapsPtr caps);

int virDomainDefFindDevice(virDomainDefPtr def,
                           const char *devAlias,
                           virDomainDeviceDefPtr dev,
                           bool reportError);

const char *virDomainChrSourceDefGetPath(virDomainChrSourceDefPtr chr);

void virDomainChrSourceDefClear(virDomainChrSourceDefPtr def);

char *virDomainObjGetMetadata(virDomainObjPtr vm,
                              int type,
                              const char *uri,
                              unsigned int flags);

int virDomainObjSetMetadata(virDomainObjPtr vm,
                            int type,
                            const char *metadata,
                            const char *key,
                            const char *uri,
                            virCapsPtr caps,
                            virDomainXMLOptionPtr xmlopt,
                            const char *stateDir,
                            const char *configDir,
                            unsigned int flags);

int
virDomainParseMemory(const char *xpath,
                     const char *units_xpath,
                     xmlXPathContextPtr ctxt,
                     unsigned long long *mem,
                     bool required,
                     bool capped);

bool virDomainDefNeedsPlacementAdvice(virDomainDefPtr def)
    ATTRIBUTE_NONNULL(1);

int virDomainDiskDefCheckDuplicateInfo(const virDomainDiskDef *a,
                                       const virDomainDiskDef *b)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

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

int virDomainDefGetVcpuPinInfoHelper(virDomainDefPtr def,
                                     int maplen,
                                     int ncpumaps,
                                     unsigned char *cpumaps,
                                     int hostcpus,
                                     virBitmapPtr autoCpuset)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(4) ATTRIBUTE_RETURN_CHECK;

bool virDomainDefHasMemballoon(const virDomainDef *def) ATTRIBUTE_NONNULL(1);

char *virDomainDefGetShortName(const virDomainDef *def) ATTRIBUTE_NONNULL(1);

int
virDomainGetBlkioParametersAssignFromDef(virDomainDefPtr def,
                                         virTypedParameterPtr params,
                                         int *nparams,
                                         int maxparams);

int virDomainDiskSetBlockIOTune(virDomainDiskDefPtr disk,
                                virDomainBlockIoTuneInfo *info);

char *
virDomainGenerateMachineName(const char *drivername,
                             int id,
                             const char *name,
                             bool privileged);

bool
virDomainNetTypeSharesHostView(const virDomainNetDef *net);

bool
virDomainDefLifecycleActionAllowed(virDomainLifecycle type,
                                   virDomainLifecycleAction action);

virNetworkPortDefPtr
virDomainNetDefToNetworkPort(virDomainDefPtr dom,
                             virDomainNetDefPtr iface);

int
virDomainNetDefActualFromNetworkPort(virDomainNetDefPtr iface,
                                     virNetworkPortDefPtr port);

virNetworkPortDefPtr
virDomainNetDefActualToNetworkPort(virDomainDefPtr dom,
                                   virDomainNetDefPtr iface);

int
virDomainNetAllocateActualDevice(virConnectPtr conn,
                                 virDomainDefPtr dom,
                                 virDomainNetDefPtr iface)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

void
virDomainNetNotifyActualDevice(virConnectPtr conn,
                               virDomainDefPtr dom,
                               virDomainNetDefPtr iface)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int
virDomainNetReleaseActualDevice(virConnectPtr conn,
                                virDomainDefPtr dom,
                                virDomainNetDefPtr iface)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int
virDomainNetBandwidthUpdate(virDomainNetDefPtr iface,
                            virNetDevBandwidthPtr newBandwidth)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2);

int
virDomainNetResolveActualType(virDomainNetDefPtr iface)
    ATTRIBUTE_NONNULL(1);


int virDomainDiskTranslateSourcePool(virDomainDiskDefPtr def);

int
virDomainDiskGetDetectZeroesMode(virDomainDiskDiscard discard,
                                 virDomainDiskDetectZeroes detect_zeroes);

bool
virDomainDefHasManagedPR(const virDomainDef *def);

bool
virDomainGraphicsDefHasOpenGL(const virDomainDef *def);

bool
virDomainGraphicsSupportsRenderNode(const virDomainGraphicsDef *graphics);

const char *
virDomainGraphicsGetRenderNode(const virDomainGraphicsDef *graphics);

bool
virDomainGraphicsNeedsAutoRenderNode(const virDomainGraphicsDef *graphics);
