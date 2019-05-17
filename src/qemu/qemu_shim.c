
#include "config.h"

#include <stdio.h>
#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>
#include <pthread.h>
#include <stdbool.h>

#include "virfile.h"
#include "virstring.h"

#define VIR_FROM_THIS VIR_FROM_QEMU

static void *eventLoop(void *opaque)
{
    bool *quit = opaque;
    while (!*quit)
        virEventRunDefaultImpl();
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t eventLoopThread;
    bool quit = false;
    virConnectPtr conn;
    virDomainPtr dom;
    char *xml = NULL;
    char *uri;

    if (argc != 3) {
        fprintf(stderr, "syntax: %s ROOT XML\n", argv[0]);
        return 1;
    }
    if (virFileReadAll(argv[2], 102400, &xml) < 0) {
        fprintf(stderr, "cannot read %s: %s\n", argv[2], virGetLastErrorMessage());
        return 1;
    }

    virFileActivateDirOverride(argv[0]);
    virEventRegisterDefaultImpl();

    pthread_create(&eventLoopThread, NULL, eventLoop, &quit);

    if (virAsprintf(&uri, "qemu:///embed?root=%s", argv[1]) < 0) {
        return 1;
    }
    conn = virConnectOpen(uri);
    if (!conn) {
        fprintf(stderr, "cannot open QEMU: %s\n", virGetLastErrorMessage());
        return 1;
    }

    dom = virDomainCreateXML(conn, xml, 0);
    if (!dom) {
        fprintf(stderr, "cannot start VM: %s\n", virGetLastErrorMessage());
        virConnectClose(conn);
        return 1;
    }

    fprintf(stderr, "Running for 10 seconds\n");
    sleep(10);

    virDomainDestroy(dom);

    virConnectClose(conn);

    return 0;
}
