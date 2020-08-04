/*
 * virresctrlpriv.h:
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef LIBVIRT_VIRRESCTRLPRIV_H_ALLOW
# error "virresctrlpriv.h may only be included by virresctrl.c or test suites"
#endif /* LIBVIRT_VIRRESCTRLPRIV_H_ALLOW */

#pragma once

#include "virresctrl.h"

virResctrlAllocPtr
virResctrlAllocGetUnused(virResctrlInfoPtr resctrl);
