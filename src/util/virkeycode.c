/*
 * Copyright (c) 2011 Lai Jiangshan
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include <config.h>
#include "virkeycode.h"
#include <stddef.h>

#include "virkeycodetable_atset1.h"
#include "virkeycodetable_atset2.h"
#include "virkeycodetable_atset3.h"
#include "virkeycodetable_linux.h"
#include "virkeycodetable_osx.h"
#include "virkeycodetable_qnum.h"
#include "virkeycodetable_usb.h"
#include "virkeycodetable_win32.h"
#include "virkeycodetable_xtkbd.h"
#include "virkeynametable_linux.h"
#include "virkeynametable_osx.h"
#include "virkeynametable_win32.h"

static const char **virKeymapNames[VIR_KEYCODE_SET_LAST] = {
    [VIR_KEYCODE_SET_LINUX] = virKeyNameTable_linux,
    [VIR_KEYCODE_SET_OSX] = virKeyNameTable_osx,
    [VIR_KEYCODE_SET_WIN32] = virKeyNameTable_win32,
};

static const unsigned short *virKeymapValues[VIR_KEYCODE_SET_LAST] = {
    [VIR_KEYCODE_SET_LINUX] = virKeyCodeTable_linux,
    /* XT is same as AT Set1 - it was included by mistake */
    [VIR_KEYCODE_SET_XT] = virKeyCodeTable_atset1,
    [VIR_KEYCODE_SET_ATSET1] = virKeyCodeTable_atset1,
    [VIR_KEYCODE_SET_ATSET2] = virKeyCodeTable_atset2,
    [VIR_KEYCODE_SET_ATSET3] = virKeyCodeTable_atset3,
    [VIR_KEYCODE_SET_OSX] = virKeyCodeTable_osx,
    [VIR_KEYCODE_SET_XT_KBD] = virKeyCodeTable_xtkbd,
    [VIR_KEYCODE_SET_USB] = virKeyCodeTable_usb,
    [VIR_KEYCODE_SET_WIN32] = virKeyCodeTable_win32,
    [VIR_KEYCODE_SET_QNUM] = virKeyCodeTable_qnum,
};

#define VIR_KEYMAP_ENTRY_MAX G_N_ELEMENTS(virKeyCodeTable_linux)

G_STATIC_ASSERT(VIR_KEYMAP_ENTRY_MAX == G_N_ELEMENTS(virKeyCodeTable_atset1));
G_STATIC_ASSERT(VIR_KEYMAP_ENTRY_MAX == G_N_ELEMENTS(virKeyCodeTable_atset2));
G_STATIC_ASSERT(VIR_KEYMAP_ENTRY_MAX == G_N_ELEMENTS(virKeyCodeTable_atset3));
G_STATIC_ASSERT(VIR_KEYMAP_ENTRY_MAX == G_N_ELEMENTS(virKeyCodeTable_osx));
G_STATIC_ASSERT(VIR_KEYMAP_ENTRY_MAX == G_N_ELEMENTS(virKeyCodeTable_xtkbd));
G_STATIC_ASSERT(VIR_KEYMAP_ENTRY_MAX == G_N_ELEMENTS(virKeyCodeTable_usb));
G_STATIC_ASSERT(VIR_KEYMAP_ENTRY_MAX == G_N_ELEMENTS(virKeyCodeTable_win32));
G_STATIC_ASSERT(VIR_KEYMAP_ENTRY_MAX == G_N_ELEMENTS(virKeyCodeTable_qnum));
G_STATIC_ASSERT(VIR_KEYMAP_ENTRY_MAX == G_N_ELEMENTS(virKeyNameTable_linux));
G_STATIC_ASSERT(VIR_KEYMAP_ENTRY_MAX == G_N_ELEMENTS(virKeyNameTable_osx));
G_STATIC_ASSERT(VIR_KEYMAP_ENTRY_MAX == G_N_ELEMENTS(virKeyNameTable_win32));

VIR_ENUM_IMPL(virKeycodeSet,
              VIR_KEYCODE_SET_LAST,
              "linux",
              "xt",
              "atset1",
              "atset2",
              "atset3",
              "os_x",
              "xt_kbd",
              "usb",
              "win32",
              "qnum",
);

int virKeycodeValueFromString(virKeycodeSet codeset,
                              const char *keyname)
{
    size_t i;

    for (i = 0; i < VIR_KEYMAP_ENTRY_MAX; i++) {
        if (!virKeymapNames[codeset] ||
            !virKeymapValues[codeset])
            continue;

        const char *name = virKeymapNames[codeset][i];

        if (name && STREQ_NULLABLE(name, keyname))
            return virKeymapValues[codeset][i];
    }

    return -1;
}


int virKeycodeValueTranslate(virKeycodeSet from_codeset,
                             virKeycodeSet to_codeset,
                             int key_value)
{
    size_t i;

    if (key_value < 0)
        return -1;


    for (i = 0; i < VIR_KEYMAP_ENTRY_MAX; i++) {
        if (virKeymapValues[from_codeset][i] == key_value)
            return virKeymapValues[to_codeset][i];
    }

    return -1;
}
