/*
 * Copyright IBM Corp. 2008
 *
 * lxc_container.h: Performs container setup tasks
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "lxc_conf.h"
#include "lxc_domain.h"
#include "security/security_manager.h"

#define LXC_DEV_MAJ_MEMORY  1
#define LXC_DEV_MIN_NULL    3
#define LXC_DEV_MIN_ZERO    5
#define LXC_DEV_MIN_FULL    7
#define LXC_DEV_MIN_RANDOM  8
#define LXC_DEV_MIN_URANDOM 9

#define LXC_DEV_MAJ_TTY     5
#define LXC_DEV_MIN_TTY     0
#define LXC_DEV_MIN_CONSOLE 1
#define LXC_DEV_MIN_PTMX    2

#define LXC_DEV_MAJ_PTY     136

#define LXC_DEV_MAJ_FUSE    10
#define LXC_DEV_MIN_FUSE    229

int lxcContainerSendContinue(int control);
int lxcContainerWaitForContinue(int control);

int lxcContainerStart(virDomainDefPtr def,
                      virSecurityManagerPtr securityDriver,
                      size_t nveths,
                      char **veths,
                      size_t npassFDs,
                      int *passFDs,
                      int control,
                      int handshakefd,
                      int *nsInheritFDs,
                      size_t nttyPaths,
                      char **ttyPaths);

int lxcContainerSetupHostdevCapsMakePath(const char *dev);

virArch lxcContainerGetAlt32bitArch(virArch arch);

int lxcContainerChown(virDomainDefPtr def, const char *path);

bool lxcIsBasicMountLocation(const char *path);
