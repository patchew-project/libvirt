/*
 * virmodule.h: APIs for dlopen'ing extension modules
 *
 * Copyright (C) 2012-2018 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

int virModuleLoad(const char *path,
                  const char *regfunc,
                  bool required);
