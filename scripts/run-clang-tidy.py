#!/usr/bin/env python3

import argparse
import json
import multiprocessing
import os
import queue
import re
import subprocess
import sys
import threading


spam = [
    re.compile("^[0-9]+ (warning|error)[s]? .*generated"),
    re.compile("^[0-9]+ warning[s]? treated as error"),
    re.compile("^Suppressed [0-9]+ warning[s]?"),
    re.compile("^Use .* to display errors from all non-system headers"),
    re.compile("Error while processing "),
    re.compile("Found compiler error"),
]


def remove_spam(output):
    retval = list()
    for line in output.split("\n"):
        if any([s.match(line) for s in spam]):
            continue
        retval.append(line)

    return "\n".join(retval)


def parse_args():
    parser = argparse.ArgumentParser(description="caching clang-tidy runner")
    parser.add_argument(
        "-p",
        dest="build_dir",
        default=".",
        help="Path to build directory")
    parser.add_argument(
        "-j",
        dest="thread_num",
        default=multiprocessing.cpu_count(),
        type=int,
        help="Number of threads to run")

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
        "stdout": remove_spam(result.stdout.strip()),
        "stderr": remove_spam(result.stderr.strip()),
    }


def worker():
    while True:
        item = items.get()
        os.chdir(item["directory"])

        print(item["file"])

        result = run_clang_tidy(item)

        with lock:
            if result["returncode"] != 0:
                findings.append(item["file"])
            if result["stdout"]:
                print(result["stdout"])
            if result["stderr"]:
                print(result["stderr"])

        items.task_done()


args = parse_args()
items = queue.Queue()
lock = threading.Lock()
findings = list()

for _ in range(args.thread_num):
    threading.Thread(target=worker, daemon=True).start()

with open(os.path.join(args.build_dir, "compile_commands.json")) as f:
    compile_commands = json.load(f)
    for compile_command in compile_commands:
        items.put(compile_command)

items.join()

if findings:
    print("Findings in %s file(s):" % len(findings))
    for finding in findings:
        print("  %s" % finding)
    sys.exit(1)
