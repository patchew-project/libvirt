/*
 * bhyve_parse_command.h: Bhyve command parser
 *
 * Copyright (C) 2016 Fabian Freyer
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

virDomainDefPtr bhyveParseCommandLineString(const char* nativeConfig,
                                            unsigned caps,
                                            virDomainXMLOptionPtr xmlopt);
