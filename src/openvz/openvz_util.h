/*
 * openvz_util.h: common util functions for managing openvz VPEs
 *
 * Copyright (C) 2012 Guido Günther
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

long openvzKBPerPages(void);
char *openvzVEGetStringParam(virDomainPtr dom, const char *param);
