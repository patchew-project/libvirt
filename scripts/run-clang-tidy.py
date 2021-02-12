#!/usr/bin/env python3

import argparse
import json
import os
import subprocess
import sys


def parse_args():
    parser = argparse.ArgumentParser(description="caching clang-tidy runner")
    parser.add_argument(
        "-p",
        dest="build_dir",
        default=".",
        help="Path to build directory")

    return parser.parse_args()


def run_clang_tidy(item):
    cmd = (
        "clang-tidy",
        "--warnings-as-errors=*",
        "-p",
        item["directory"],
        item["file"])
    result = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True)
    return {
        "returncode": result.returncode,
        "stdout": result.stdout.strip(),
        "stderr": result.stderr.strip(),
    }


def worker():
    for item in items:
        os.chdir(item["directory"])

        print(item["file"])

        result = run_clang_tidy(item)

        if result["returncode"] != 0:
            findings.append(item["file"])
        if result["stdout"]:
            print(result["stdout"])
        if result["stderr"]:
            print(result["stderr"])


args = parse_args()
findings = list()

with open(os.path.join(args.build_dir, "compile_commands.json")) as f:
    items = json.load(f)

worker()

if findings:
    print("Findings in %s file(s):" % len(findings))
    for finding in findings:
        print("  %s" % finding)
    sys.exit(1)
