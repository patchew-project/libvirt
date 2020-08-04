/*
 * virsh-util.h: helpers for virsh
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "virsh.h"

#include <libxml/parser.h>
#include <libxml/xpath.h>

virDomainPtr
virshLookupDomainBy(vshControl *ctl,
                    const char *name,
                    unsigned int flags);

virDomainPtr
virshCommandOptDomainBy(vshControl *ctl,
                        const vshCmd *cmd,
                        const char **name,
                        unsigned int flags);

virDomainPtr
virshCommandOptDomain(vshControl *ctl,
                      const vshCmd *cmd,
                      const char **name);

typedef virDomain virshDomain;

void
virshDomainFree(virDomainPtr dom);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(virshDomain, virshDomainFree);

typedef virSecret virshSecret;
void
virshSecretFree(virSecretPtr secret);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(virshSecret, virshSecretFree);

void
virshDomainCheckpointFree(virDomainCheckpointPtr chk);

void
virshDomainSnapshotFree(virDomainSnapshotPtr snap);

int
virshDomainState(vshControl *ctl,
                 virDomainPtr dom,
                 int *reason);

int
virshStreamSink(virStreamPtr st,
                const char *bytes,
                size_t nbytes,
                void *opaque);

typedef struct _virshStreamCallbackData virshStreamCallbackData;
typedef virshStreamCallbackData *virshStreamCallbackDataPtr;
struct _virshStreamCallbackData {
    vshControl *ctl;
    int fd;
};

int
virshStreamSource(virStreamPtr st,
                  char *bytes,
                  size_t nbytes,
                  void *opaque);

int
virshStreamSourceSkip(virStreamPtr st,
                      long long offset,
                      void *opaque);

int
virshStreamSkip(virStreamPtr st,
                long long offset,
                void *opaque);

int
virshStreamInData(virStreamPtr st,
                  int *inData,
                  long long *offset,
                  void *opaque);

int
virshDomainGetXMLFromDom(vshControl *ctl,
                         virDomainPtr dom,
                         unsigned int flags,
                         xmlDocPtr *xml,
                         xmlXPathContextPtr *ctxt)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(4)
    ATTRIBUTE_NONNULL(5) G_GNUC_WARN_UNUSED_RESULT;

int
virshDomainGetXML(vshControl *ctl,
                  const vshCmd *cmd,
                  unsigned int flags,
                  xmlDocPtr *xml,
                  xmlXPathContextPtr *ctxt)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) ATTRIBUTE_NONNULL(4)
    ATTRIBUTE_NONNULL(5) G_GNUC_WARN_UNUSED_RESULT;
