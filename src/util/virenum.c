/*
 * virenum.c: enum value conversion helpers
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <config.h>

#include "virenum.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_ENUM_IMPL(virTristateBool,
              VIR_TRISTATE_BOOL_LAST,
              "default",
              "yes",
              "no",
);

VIR_ENUM_IMPL(virTristateSwitch,
              VIR_TRISTATE_SWITCH_LAST,
              "default",
              "on",
              "off",
);


virTristateBool
virTristateBoolFromBool(bool val)
{
    if (val)
        return VIR_TRISTATE_BOOL_YES;
    else
        return VIR_TRISTATE_BOOL_NO;
}


virTristateSwitch
virTristateSwitchFromBool(bool val)
{
    if (val)
        return VIR_TRISTATE_SWITCH_ON;
    else
        return VIR_TRISTATE_SWITCH_OFF;
}


int
virEnumFromString(const char * const *types,
                  unsigned int ntypes,
                  const char *type)
{
    size_t i;
    if (!type)
        return -1;

    for (i = 0; i < ntypes; i++)
        if (STREQ(types[i], type))
            return i;

    return -1;
}


const char *
virEnumToString(const char * const *types,
                unsigned int ntypes,
                int type)
{
    if (type < 0 || type >= ntypes)
        return NULL;

    return types[type];
}
