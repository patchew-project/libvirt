/*
 * virenum.h: enum value conversion helpers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "internal.h"
#include "virenum.generated.h"

int
virEnumFromString(const char * const *types,
                  unsigned int ntypes,
                  const char *type);

const char *
virEnumToString(const char * const *types,
                unsigned int ntypes,
                int type);

typedef virTristateBoolType virTristateBool;
typedef virTristateSwitchType virTristateSwitch;

virTristateBool virTristateBoolFromBool(bool val);
virTristateSwitch virTristateSwitchFromBool(bool val);

/* the two enums must be in sync to be able to use helpers interchangeably in
 * some special cases */
G_STATIC_ASSERT((int)VIR_TRISTATE_BOOL_YES == (int)VIR_TRISTATE_SWITCH_ON);
G_STATIC_ASSERT((int)VIR_TRISTATE_BOOL_NO == (int)VIR_TRISTATE_SWITCH_OFF);
G_STATIC_ASSERT((int)VIR_TRISTATE_BOOL_ABSENT == (int)VIR_TRISTATE_SWITCH_ABSENT);
