#!/usr/bin/env python3
#
# Copyright (C) 2013-2019 Red Hat, Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#

import re
import sys

drvfiles = []
symfiles = []
for arg in sys.argv:
    if arg.endswith(".h"):
        drvfiles.append(arg)
    else:
        symfiles.append(arg)

symbols = {}

for symfile in symfiles:
    with open(symfile, "r") as fh:
        for line in fh:
            m = re.search(r'''^\s*(vir\w+)\s*;\s*$''', line)
            if m is not None:
                symbols[m.group(1)] = True

status = 0
for drvfile in drvfiles:
    with open(drvfile, "r") as fh:
        for line in fh:
            m = re.search(r'''\*(virDrv\w+)\s*\)''', line)
            if m is not None:
                drv = m.group(1)

                skip = [
                    "virDrvStateInitialize",
                    "virDrvStateCleanup",
                    "virDrvStateReload",
                    "virDrvStateStop",
                    "virDrvConnectSupportsFeature",
                    "virDrvConnectURIProbe",
                    "virDrvDomainMigratePrepare",
                    "virDrvDomainMigratePrepare2",
                    "virDrvDomainMigratePrepare3",
                    "virDrvDomainMigratePrepare3Params",
                    "virDrvDomainMigratePrepareTunnel",
                    "virDrvDomainMigratePrepareTunnelParams",
                    "virDrvDomainMigratePrepareTunnel3",
                    "virDrvDomainMigratePrepareTunnel3Params",
                    "virDrvDomainMigratePerform",
                    "virDrvDomainMigratePerform3",
                    "virDrvDomainMigratePerform3Params",
                    "virDrvDomainMigrateConfirm",
                    "virDrvDomainMigrateConfirm3",
                    "virDrvDomainMigrateConfirm3Params",
                    "virDrvDomainMigrateBegin",
                    "virDrvDomainMigrateBegin3",
                    "virDrvDomainMigrateBegin3Params",
                    "virDrvDomainMigrateFinish",
                    "virDrvDomainMigrateFinish2",
                    "virDrvDomainMigrateFinish3",
                    "virDrvDomainMigrateFinish3Params",
                    "virDrvStreamInData",
                ]
                if drv in skip:
                    continue

                sym = drv.replace("virDrv", "vir")

                if sym not in symbols:
                    print("Driver method name %s doesn't match public API" %
                          drv)
                    status = 1
                continue

            m = re.search(r'''(\*vir\w+)\s*\)''', line)
            if m is not None:
                name = m.group(1)
                print("Bogus name %s" % name)
                status = 1
                continue

            m = re.search(r'''^\s*(virDrv\w+)\s+(\w+);\s*''', line)
            if m is not None:
                drv = m.group(1)
                field = m.group(2)

                tmp = drv.replace("virDrv", "")
                if tmp.startswith("NWFilter"):
                    tmp = "nwfilter" + tmp[8:]
                tmp = tmp[0:1].lower() + tmp[1:]

                if tmp != field:
                    print("Driver struct field %s should be named %s" %
                          (field, tmp))
                    status = 1

sys.exit(status)
