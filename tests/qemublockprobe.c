/*
 * qemublockprobe.c: image backing chain prober
 *
 * Copyright (C) 2019 Red Hat, Inc.
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

#include <config.h>

#include <stdio.h>
#include <stdbool.h>

#include "util/virfile.h"
#include "util/virlog.h"
#include "util/virstoragefile.h"

#include "virgettext.h"

#define VIR_FROM_THIS VIR_FROM_QEMU


static void
print_source(virStorageSourcePtr src)
{
    size_t i;

    g_print("type: %s (%d)\n", virStorageTypeToString(src->type), src->type);
    g_print("path: %s\n", src->path);
    g_print("format: %s (%d)\n", virStorageFileFormatTypeToString(src->format), src->format);
    g_print("protocol: %s' (%d)\n", virStorageNetProtocolTypeToString(src->protocol), src->protocol);
    for (i = 0; i < src->nhosts; i++) {
        virStorageNetHostDefPtr h = src->hosts + i;

        g_print("host %zu: name: '%s', port: '%u', transport: '%s'(%d), socket: '%s'\n",
                i, h->name, h->port, virStorageNetHostTransportTypeToString(h->transport),
                h->transport, h->socket);
    }
    if (src->sliceStorage)
        g_print("slice type: storage, offset: %llu, size: %llu\n",
                src->sliceStorage->offset, src->sliceStorage->size);
    if (src->backingStoreRaw)
        g_print("backing store raw: %s\n", src->backingStoreRaw);
    if (src->externalDataStoreRaw)
        g_print("external store raw: %s\n", src->externalDataStoreRaw);
    if (src->relPath)
        g_print("relative path: %s\n", src->relPath);

    g_print("\n");
}


int main(int argc, char **argv)
{
    g_autofree char *path = NULL;
    g_autofree char *format = NULL;
    g_autoptr(GError) error = NULL;
    bool verbose = false;
    g_autoptr(virStorageSource) src = NULL;
    GOptionContext *ctx;
    virStorageSourcePtr n;
    int ret = 1;

    GOptionEntry entries[] = {
        { "path", 'p', 0, G_OPTION_ARG_STRING, &path, "path to image", "DIR" },
        { "format", 'f', 0, G_OPTION_ARG_STRING, &format, "format of image", "DIR" },
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Verbose output", NULL },
        { 0 }
    };


    ctx = g_option_context_new("- inspect an image");
    g_option_context_add_main_entries(ctx, entries, PACKAGE);
    if (!g_option_context_parse(ctx, &argc, &argv, &error)) {
        g_printerr("%s: option parsing failed: %s\n",
                   argv[0], error->message);
        return 1;
    }

    if (!path) {
        g_printerr("%s: missing path\n", argv[0]);
        return 1;
    }

    if (virErrorInitialize() < 0) {
        g_printerr("%s: failed to initialize error handling\n", argv[0]);
        return 1;
    }

    virLogSetFromEnv();
    virFileActivateDirOverrideForProg(argv[0]);

    if (!(src = virStorageSourceNew()))
        goto cleanup;

    src->path = g_steal_pointer(&path);
    src->type = VIR_STORAGE_TYPE_FILE;

    if (format &&
        (src->format = virStorageFileFormatTypeFromString(format)) < 0) {
        g_printerr("%s: unknown format '%s'\n", argv[0], format);
        goto cleanup;
    }

    if (virStorageFileGetMetadata(src, -1, -1, true) < 0)
        goto cleanup;

    for (n = src; n; n = n->backingStore)
        print_source(n);

    ret = 0;

 cleanup:
    if (virGetLastErrorCode() != VIR_ERR_OK)
        g_printerr("%s: libvirt error: %s\n", argv[0], virGetLastErrorMessage());

    return ret;
}
