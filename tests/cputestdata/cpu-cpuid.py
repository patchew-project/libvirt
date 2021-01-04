#!/usr/bin/env python3

import argparse
import os
import sys
import json
import xmltodict
import xml.etree.ElementTree


def checkCPUIDFeature(cpuData, feature):
    eax_in = feature["eax_in"]
    ecx_in = feature["ecx_in"]
    eax = feature["eax"]
    ebx = feature["ebx"]
    ecx = feature["ecx"]
    edx = feature["edx"]

    if "cpuid" not in cpuData:
        return False

    cpuid = cpuData["cpuid"]
    if eax_in not in cpuid or ecx_in not in cpuid[eax_in]:
        return False

    leaf = cpuid[eax_in][ecx_in]
    return ((eax > 0 and leaf["eax"] & eax == eax) or
            (ebx > 0 and leaf["ebx"] & ebx == ebx) or
            (ecx > 0 and leaf["ecx"] & ecx == ecx) or
            (edx > 0 and leaf["edx"] & edx == edx))


def checkMSRFeature(cpuData, feature):
    index = feature["index"]
    edx = feature["edx"]
    eax = feature["eax"]

    if "msr" not in cpuData:
        return False

    msr = cpuData["msr"]
    if index not in msr:
        return False

    msr = msr[index]
    return ((edx > 0 and msr["edx"] & edx == edx) or
            (eax > 0 and msr["eax"] & eax == eax))


def checkFeature(cpuData, feature):
    if feature["type"] == "cpuid":
        return checkCPUIDFeature(cpuData, feature)

    if feature["type"] == "msr":
        return checkMSRFeature(cpuData, feature)


def addCPUIDFeature(cpuData, feature):
    if "cpuid" not in cpuData:
        cpuData["cpuid"] = {}
    cpuid = cpuData["cpuid"]

    if feature["eax_in"] not in cpuid:
        cpuid[feature["eax_in"]] = {}
    leaf = cpuid[feature["eax_in"]]

    if feature["ecx_in"] not in leaf:
        leaf[feature["ecx_in"]] = {"eax": 0, "ebx": 0, "ecx": 0, "edx": 0}
    leaf = leaf[feature["ecx_in"]]

    for reg in ["eax", "ebx", "ecx", "edx"]:
        leaf[reg] |= feature[reg]


def addMSRFeature(cpuData, feature):
    if "msr" not in cpuData:
        cpuData["msr"] = {}
    msr = cpuData["msr"]

    if feature["index"] not in msr:
        msr[feature["index"]] = {"edx": 0, "eax": 0}
    msr = msr[feature["index"]]

    for reg in ["edx", "eax"]:
        msr[reg] |= feature[reg]


def addFeature(cpuData, feature):
    if feature["type"] == "cpuid":
        addCPUIDFeature(cpuData, feature)
    elif feature["type"] == "msr":
        addMSRFeature(cpuData, feature)


def parseQemu(path, features):
    cpuData = {}
    with open(path, "r") as f:
        data, pos = json.JSONDecoder().raw_decode(f.read())

    for (prop, val) in data["return"]["model"]["props"].items():
        if val and prop in features:
            addFeature(cpuData, features[prop])

    return cpuData


def parseCPUData(path):
    cpuData = {}
    with open(path, "rb") as f:
        data = xmltodict.parse(f)

    for leaf in data["cpudata"]["cpuid"]:
        feature = {"type": "cpuid"}
        feature["eax_in"] = int(leaf["@eax_in"], 0)
        feature["ecx_in"] = int(leaf["@ecx_in"], 0)
        for reg in ["eax", "ebx", "ecx", "edx"]:
            feature[reg] = int(leaf["@" + reg], 0)

        addFeature(cpuData, feature)

    if "msr" in data["cpudata"]:
        if not isinstance(data["cpudata"]["msr"], list):
            data["cpudata"]["msr"] = [data["cpudata"]["msr"]]

        for msr in data["cpudata"]["msr"]:
            feature = {"type": "msr"}
            feature["index"] = int(msr["@index"], 0)
            feature["edx"] = int(msr["@edx"], 0)
            feature["eax"] = int(msr["@eax"], 0)

            addFeature(cpuData, feature)

    return cpuData


def parseMap():
    path = os.path.dirname(sys.argv[0])
    path = os.path.join(path, "..", "..", "src", "cpu_map", "x86_features.xml")

    cpuMap = dict()
    for f in xml.etree.ElementTree.parse(path).getroot().iter("feature"):
        if f[0].tag == "cpuid":
            reg_list = ["eax_in", "ecx_in", "eax", "ebx", "ecx", "edx"]
        elif f[0].tag == "msr":
            reg_list = ["index", "eax", "edx"]
        else:
            continue

        feature = {"type": f[0].tag}
        for reg in reg_list:
            feature[reg] = int(f[0].attrib.get(reg, "0"), 0)
        cpuMap[f.attrib["name"]] = feature
    return cpuMap


def formatCPUData(cpuData, path, comment):
    print(path)
    with open(path, "w") as f:
        f.write("<!-- " + comment + " -->\n")
        f.write("<cpudata arch='x86'>\n")

        cpuid = cpuData["cpuid"]
        for eax_in in sorted(cpuid.keys()):
            for ecx_in in sorted(cpuid[eax_in].keys()):
                leaf = cpuid[eax_in][ecx_in]
                line = ("  <cpuid eax_in='0x%08x' ecx_in='0x%02x' "
                        "eax='0x%08x' ebx='0x%08x' "
                        "ecx='0x%08x' edx='0x%08x'/>\n")
                f.write(line % (
                        eax_in, ecx_in,
                        leaf["eax"], leaf["ebx"], leaf["ecx"], leaf["edx"]))

        if "msr" in cpuData:
            msr = cpuData["msr"]
            for index in sorted(msr.keys()):
                f.write("  <msr index='0x%x' edx='0x%08x' eax='0x%08x'/>\n" %
                        (index, msr[index]['edx'], msr[index]['eax']))

        f.write("</cpudata>\n")


def diff(args):
    cpuMap = parseMap()

    for jsonFile in args.json_files:
        cpuDataFile = jsonFile.replace(".json", ".xml")
        enabledFile = jsonFile.replace(".json", "-enabled.xml")
        disabledFile = jsonFile.replace(".json", "-disabled.xml")

        cpuData = parseCPUData(cpuDataFile)
        qemu = parseQemu(jsonFile, cpuMap)

        enabled = dict()
        disabled = dict()
        for feature in cpuMap.values():
            if checkFeature(qemu, feature):
                addFeature(enabled, feature)
            elif checkFeature(cpuData, feature):
                addFeature(disabled, feature)

        formatCPUData(enabled, enabledFile, "Features enabled by QEMU")
        formatCPUData(disabled, disabledFile, "Features disabled by QEMU")


def main():
    parser = argparse.ArgumentParser(description="Diff cpuid results")
    subparsers = parser.add_subparsers(dest="action", required=True)
    diffparser = subparsers.add_parser(
        "diff",
        help="Diff json description of CPU model against known features.")
    diffparser.add_argument(
        "json_files",
        nargs="+",
        metavar="FILE",
        type=os.path.realpath,
        help="Path to one or more json CPU model descriptions.")
    args = parser.parse_args()

    diff(args)
    exit(0)


if __name__ == "__main__":
    main()
