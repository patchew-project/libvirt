/*
 * virt-host-validate.c: Sanity check a hypervisor host
 *
 * Copyright (C) 2012 Red Hat, Inc.
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
 *
 */

#include <config.h>

#ifdef HAVE_LIBINTL_H
# include <libintl.h>
#endif /* HAVE_LIBINTL_H */

#include "internal.h"
#include "virgettext.h"

#include "virt-host-validate-common.h"
#if WITH_QEMU
# include "virt-host-validate-qemu.h"
#endif
#if WITH_LXC
# include "virt-host-validate-lxc.h"
#endif
#if WITH_BHYVE
# include "virt-host-validate-bhyve.h"
#endif

static void
show_version(FILE *out, const char *argv0)
{
    fprintf(out, "version: %s %s\n", argv0, VERSION);
}


int
main(int argc, char **argv)
{
    const char *hvname = NULL;
    int ret = EXIT_SUCCESS;
    bool quiet = false;
    bool usedHvname = false;
    bool version = false;
    GOptionEntry opt[] = {
        { "version", 'v', 0,
          G_OPTION_ARG_NONE, &version,
          _("Print version"), NULL },
        { "quiet", 'q', 0,
          G_OPTION_ARG_NONE, &quiet,
          _("Don't display progress information"), NULL },
        { NULL, 0, 0, 0, NULL, NULL, NULL },
    };
    g_autoptr(GOptionContext) optctx = NULL;
    g_autoptr(GError) error = NULL;

    if (virGettextInitialize() < 0)
        return EXIT_FAILURE;

    optctx = g_option_context_new(_("HV-TYPE - validate host OS suppport"));
    g_option_context_add_main_entries(optctx, opt, PACKAGE);
    g_option_context_set_description(optctx,
                                     "Hypervisor types:\n"
                                     "\n"
                                     "   - qemu\n"
                                     "   - lxc\n"
                                     "   - bhyve\n");

    if (!g_option_context_parse(optctx, &argc, &argv, &error)) {
        fprintf(stderr, _("%s: option parsing failed: %s\n"), argv[0], error->message);
        return ret;
    }

    if (version) {
        show_version(stdout, argv[0]);
        return EXIT_SUCCESS;
    }

    if (argc > 2) {
        fprintf(stderr, _("%s: too many command line arguments\n"), argv[0]);
        g_autofree char *help = g_option_context_get_help(optctx, TRUE, NULL);
        fprintf(stderr, "%s", help);
        return EXIT_FAILURE;
    }

    if (argc == 2)
        hvname = argv[1];

    virHostMsgSetQuiet(quiet);

#if WITH_QEMU
    if (!hvname || STREQ(hvname, "qemu")) {
        usedHvname = true;
        if (virHostValidateQEMU() < 0)
            ret = EXIT_FAILURE;
    }
#endif

#if WITH_LXC
    if (!hvname || STREQ(hvname, "lxc")) {
        usedHvname = true;
        if (virHostValidateLXC() < 0)
            ret = EXIT_FAILURE;
    }
#endif

#if WITH_BHYVE
    if (!hvname || STREQ(hvname, "bhyve")) {
        usedHvname = true;
        if (virHostValidateBhyve() < 0)
            ret = EXIT_FAILURE;
    }
#endif

    if (hvname && !usedHvname) {
        fprintf(stderr, _("%s: unsupported hypervisor name %s\n"),
                argv[0], hvname);
        return EXIT_FAILURE;
    }

    return ret;
}
