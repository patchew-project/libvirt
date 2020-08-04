/*
 * virgettext.c: gettext helper routines
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <config.h>

#include <locale.h>
#ifdef HAVE_XLOCALE_H
# include <xlocale.h>
#endif

#include "configmake.h"
#include "internal.h"
#include "virgettext.h"


/**
 * virGettextInitialize:
 *
 * Initialize standard gettext setup
 * Returns -1 on fatal error
 */
int
virGettextInitialize(void)
{
#if HAVE_LIBINTL_H
    if (!setlocale(LC_ALL, "")) {
        perror("setlocale");
        /* failure to setup locale is not fatal */
    }

    if (!bindtextdomain(PACKAGE, LOCALEDIR)) {
        perror("bindtextdomain");
        return -1;
    }

    if (!textdomain(PACKAGE)) {
        perror("textdomain");
        return -1;
    }
#endif /* HAVE_LIBINTL_H */
    return 0;
}
