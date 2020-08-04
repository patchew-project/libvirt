/*
 * virenum.h: enum value conversion helpers
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "internal.h"

int
virEnumFromString(const char * const *types,
                  unsigned int ntypes,
                  const char *type);

const char *
virEnumToString(const char * const *types,
                unsigned int ntypes,
                int type);

#define VIR_ENUM_IMPL(name, lastVal, ...) \
    static const char *const name ## TypeList[] = { __VA_ARGS__ }; \
    const char *name ## TypeToString(int type) { \
        return virEnumToString(name ## TypeList, \
                               G_N_ELEMENTS(name ## TypeList), \
                               type); \
    } \
    int name ## TypeFromString(const char *type) { \
        return virEnumFromString(name ## TypeList, \
                                 G_N_ELEMENTS(name ## TypeList), \
                                 type); \
    } \
    G_STATIC_ASSERT(G_N_ELEMENTS(name ## TypeList) == lastVal)

#define VIR_ENUM_DECL(name) \
    const char *name ## TypeToString(int type); \
    int name ## TypeFromString(const char*type)

typedef enum {
    VIR_TRISTATE_BOOL_ABSENT = 0,
    VIR_TRISTATE_BOOL_YES,
    VIR_TRISTATE_BOOL_NO,

    VIR_TRISTATE_BOOL_LAST
} virTristateBool;

typedef enum {
    VIR_TRISTATE_SWITCH_ABSENT = 0,
    VIR_TRISTATE_SWITCH_ON,
    VIR_TRISTATE_SWITCH_OFF,

    VIR_TRISTATE_SWITCH_LAST
} virTristateSwitch;

VIR_ENUM_DECL(virTristateBool);
VIR_ENUM_DECL(virTristateSwitch);

virTristateBool virTristateBoolFromBool(bool val);
virTristateSwitch virTristateSwitchFromBool(bool val);

/* the two enums must be in sync to be able to use helpers interchangeably in
 * some special cases */
G_STATIC_ASSERT((int)VIR_TRISTATE_BOOL_YES == (int)VIR_TRISTATE_SWITCH_ON);
G_STATIC_ASSERT((int)VIR_TRISTATE_BOOL_NO == (int)VIR_TRISTATE_SWITCH_OFF);
G_STATIC_ASSERT((int)VIR_TRISTATE_BOOL_ABSENT == (int)VIR_TRISTATE_SWITCH_ABSENT);
