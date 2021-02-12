#!/usr/bin/env python3

import argparse
import json
import multiprocessing
import os
import queue
import subprocess
import sys
import threading


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
        "stdout": result.stdout.strip(),
        "stderr": result.stderr.strip(),
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
