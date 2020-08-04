/*
 * nwfilter_driver.h: core driver for nwfilter APIs
 *                    (based on storage driver)
 *
 * Copyright (C) 2006-2008 Red Hat, Inc.
 * Copyright (C) 2006-2008 Daniel P. Berrange
 * Copyright (C) 2010 IBM Corporation
 * Copyright (C) 2010 Stefan Berger
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "nwfilter_params.h"
#include "nwfilter_conf.h"

int nwfilterRegister(void);
