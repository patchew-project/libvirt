/*
 * rename.c
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <libvirt/libvirt.h>

int main(int argc, char **argv)
{
    virConnectPtr conn = NULL; /* the hypervisor connection */
    virDomainPtr dom = NULL;   /* the domain being checked */
    int ret = EXIT_FAILURE;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <current_domname> <temporary_domname>\n",
                argv[0]);
        goto error;
    }

    conn = virConnectOpen(NULL);
    if (conn == NULL) {
        fprintf(stderr, "Failed to connect to hypervisor\n");
        goto error;
    }

    dom = virDomainLookupByName(conn, argv[1]);
    if (dom == NULL) {
        fprintf(stderr, "Failed to find domain\n");
        goto error;
    }

    printf("Before first rename: %s\n", virDomainGetName(dom));

    /* Get the information */
    ret = virDomainRename(dom, argv[2], 0);
    if (ret < 0) {
        fprintf(stderr, "Failed to rename domain\n");
        goto error;
    }

    printf("After first rename: %s\n", virDomainGetName(dom));

    /* Get the information */
    ret = virDomainRename(dom, argv[1], 0);
    if (ret < 0) {
        fprintf(stderr, "Failed to rename domain\n");
        goto error;
    }

    printf("After second rename: %s\n", virDomainGetName(dom));

 error:
    if (dom != NULL)
        virDomainFree(dom);
    if (conn != NULL)
        virConnectClose(conn);
    return ret;
}
