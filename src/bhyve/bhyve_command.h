/*
 * bhyve_command.h: bhyve command generation
 *
 * Copyright (C) 2014 Roman Bogorodskiy
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include "bhyve_domain.h"
#include "bhyve_utils.h"

#include "domain_conf.h"
#include "vircommand.h"

#define BHYVE_CONFIG_FORMAT_ARGV "bhyve-argv"

virCommandPtr virBhyveProcessBuildBhyveCmd(bhyveConnPtr driver,
                                           virDomainDefPtr def,
                                           bool dryRun);

virCommandPtr
virBhyveProcessBuildDestroyCmd(bhyveConnPtr driver,
                               virDomainDefPtr def);

virCommandPtr
virBhyveProcessBuildLoadCmd(bhyveConnPtr driver, virDomainDefPtr def,
                            const char *devmap_file, char **devicesmap_out);
