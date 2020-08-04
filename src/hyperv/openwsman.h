/*
 * openwsman.h: workarounds for bugs in openwsman
 *
 * Copyright (C) 2011 Matthias Bolte <matthias.bolte@googlemail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

/* Workaround openwsman <= 2.2.6 unconditionally defining optarg. Just pretend
 * that u/os.h was already included. Need to explicitly include time.h because
 * wsman-xml-serializer.h needs it and u/os.h would have included it. */
#include <time.h>
#define _LIBU_OS_H_
#include <wsman-api.h>

/* wsman-xml-serializer.h in openwsman <= 2.2.6 is missing this defines */
#ifndef SER_NS_INT8
# define SER_NS_INT8(ns, n, x) SER_NS_INT8_FLAGS(ns, n, x, 0)
#endif
#ifndef SER_NS_INT16
# define SER_NS_INT16(ns, n, x) SER_NS_INT16_FLAGS(ns, n, x, 0)
#endif
#ifndef SER_NS_INT32
# define SER_NS_INT32(ns, n, x) SER_NS_INT32_FLAGS(ns, n, x, 0)
#endif
#ifndef SER_NS_INT64
# define SER_NS_INT64(ns, n, x) SER_NS_INT64_FLAGS(ns, n, x, 0)
#endif

/* wsman-xml.h */
WsXmlDocH ws_xml_create_doc(const char *rootNsUri, const char *rootName);
WsXmlNodeH xml_parser_get_root(WsXmlDocH doc);
