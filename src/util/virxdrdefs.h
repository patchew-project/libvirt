/*
 * virxdrdefs.h
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

/* The portablexdr implementation lacks IXDR_PUT_U_INT32 and IXDR_GET_U_INT32
 */
#ifndef IXDR_PUT_U_INT32
# define IXDR_PUT_U_INT32 IXDR_PUT_U_LONG
#endif
#ifndef IXDR_GET_U_INT32
# define IXDR_GET_U_INT32 IXDR_GET_U_LONG
#endif
