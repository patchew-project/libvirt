/*
 * virmacaddr.h: MAC address handling
 *
 * Copyright (C) 2006-2013 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

#define VIR_MAC_BUFLEN 6
#define VIR_MAC_HEXLEN (VIR_MAC_BUFLEN * 2)
#define VIR_MAC_PREFIX_BUFLEN 3
#define VIR_MAC_STRING_BUFLEN (VIR_MAC_BUFLEN * 3)

typedef struct _virMacAddr virMacAddr;
typedef virMacAddr *virMacAddrPtr;

struct _virMacAddr {
    unsigned char addr[VIR_MAC_BUFLEN];
};
/* This struct is used as a part of a larger struct that is
 * overlaid on an ethernet packet captured with libpcap, so it
 * must not have any extra members added - it must remain exactly
 * 6 bytes in length.
 */
G_STATIC_ASSERT(sizeof(struct _virMacAddr) == 6);


int virMacAddrCompare(const char *mac1, const char *mac2);
int virMacAddrCmp(const virMacAddr *mac1, const virMacAddr *mac2);
int virMacAddrCmpRaw(const virMacAddr *mac1,
                     const unsigned char s[VIR_MAC_BUFLEN]);
void virMacAddrSet(virMacAddrPtr dst, const virMacAddr *src);
void virMacAddrSetRaw(virMacAddrPtr dst, const unsigned char s[VIR_MAC_BUFLEN]);
void virMacAddrGetRaw(const virMacAddr *src, unsigned char dst[VIR_MAC_BUFLEN]);
const char *virMacAddrFormat(const virMacAddr *addr,
                             char *str);
void virMacAddrGenerate(const unsigned char prefix[VIR_MAC_PREFIX_BUFLEN],
                        virMacAddrPtr addr) G_GNUC_NO_INLINE;
int virMacAddrParse(const char* str,
                    virMacAddrPtr addr) G_GNUC_WARN_UNUSED_RESULT;
int virMacAddrParseHex(const char* str,
                       virMacAddrPtr addr)
    ATTRIBUTE_NONNULL(1) ATTRIBUTE_NONNULL(2) G_GNUC_WARN_UNUSED_RESULT;
bool virMacAddrIsUnicast(const virMacAddr *addr);
bool virMacAddrIsMulticast(const virMacAddr *addr);
bool virMacAddrIsBroadcastRaw(const unsigned char s[VIR_MAC_BUFLEN]);
void virMacAddrFree(virMacAddrPtr addr);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virMacAddr, virMacAddrFree);
