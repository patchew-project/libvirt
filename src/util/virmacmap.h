/*
 * virmacmap.h: MAC address <-> Domain name mapping
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

typedef struct virMacMap virMacMap;
typedef virMacMap *virMacMapPtr;

char *
virMacMapFileName(const char *dnsmasqStateDir,
                  const char *bridge);

virMacMapPtr virMacMapNew(const char *file);

int virMacMapAdd(virMacMapPtr mgr,
                 const char *domain,
                 const char *mac);

int virMacMapRemove(virMacMapPtr mgr,
                    const char *domain,
                    const char *mac);

const char *const *virMacMapLookup(virMacMapPtr mgr,
                                   const char *domain);

int virMacMapWriteFile(virMacMapPtr mgr,
                       const char *filename);

int virMacMapDumpStr(virMacMapPtr mgr,
                     char **str);
