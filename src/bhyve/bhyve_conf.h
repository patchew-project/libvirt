/*
 * bhyve_conf.h: bhyve config file
 *
 * Copyright (C) 2017 Roman Bogorodskiy
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "bhyve_utils.h"

virBhyveDriverConfigPtr virBhyveDriverConfigNew(void);
virBhyveDriverConfigPtr virBhyveDriverGetConfig(bhyveConnPtr driver);
int virBhyveLoadDriverConfig(virBhyveDriverConfigPtr cfg,
                             const char *filename);

typedef struct _bhyveDomainCmdlineDef bhyveDomainCmdlineDef;
typedef bhyveDomainCmdlineDef *bhyveDomainCmdlineDefPtr;
struct _bhyveDomainCmdlineDef {
    size_t num_args;
    char **args;
};

void bhyveDomainCmdlineDefFree(bhyveDomainCmdlineDefPtr def);
