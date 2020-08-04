/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef LIBVIRT_VIRERRORPRIV_H_ALLOW
# error "virerrorpriv.h may only be included by virerror.c or its test suite"
#endif /* LIBVIRT_VIRERRORPRIV_H_ALLOW */

#pragma once

const char *
virErrorMsg(virErrorNumber error,
            const char *info);
