/* packet-libvirt.h --- Libvirt packet dissector header file.
 *
 * Copyright (C) 2013 Yuto KAWAMURA(kawamuray) <kawamuray.dadada@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

void proto_register_libvirt(void);
void proto_reg_handoff_libvirt(void);
