#!/usr/bin/env python3

import argparse
import hashlib
import json
import multiprocessing
import os
import queue
import re
import shlex
import subprocess
import sys
import threading
import time


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
    parser.add_argument(
        "--cache",
        dest="cache",
        help="Path to cache directory")

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


def cache_name(item):
    if not args.cache:
        return None

    cmd = shlex.split(item["command"])
    for index, element in enumerate(cmd):
        if element == "-o":
            cmd[index + 1] = "/dev/stdout"
            continue
        if element == "-MD":
            cmd[index] = None
        if element in ("-MQ", "-MF"):
            cmd[index] = None
            cmd[index + 1] = None
    cmd = [c for c in cmd if c is not None]
    cmd.append("-E")

    result = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        universal_newlines=True)

    if result.returncode != 0:
        return None

    hashsum = hashlib.sha256()
    hashsum.update(item["command"].encode())
    hashsum.update(result.stdout.encode())

    basename = "".join([c if c.isalnum() else "_" for c in item["output"]])
    return os.path.join(args.cache, "%s-%s" % (basename, hashsum.hexdigest()))


def cache_read(filename):
    if filename is None:
        return None

    try:
        with open(filename) as f:
            return json.load(f)
    except FileNotFoundError:
        pass
    except json.decoder.JSONDecodeError:
        pass
    return None


def cache_write(filename, result):
    if filename is None:
        return

    with open(filename, "w") as f:
        json.dump(result, f)


def worker():
    while True:
        item = items.get()
        os.chdir(item["directory"])

        cache = cache_name(item)
        result = cache_read(cache)
        with lock:
            print(item["file"], "" if result is None else "(from cache)")

        if result is None:
            result = run_clang_tidy(item)

        cache_write(cache, result)

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

if args.cache:
    args.cache = os.path.abspath(args.cache)
    os.makedirs(args.cache, exist_ok=True)

for _ in range(args.thread_num):
    threading.Thread(target=worker, daemon=True).start()

with open(os.path.join(args.build_dir, "compile_commands.json")) as f:
    compile_commands = json.load(f)
    for compile_command in compile_commands:
        items.put(compile_command)

items.join()

if args.cache:
    cutoffdate = time.time() - 7 * 24 * 60 * 60
    for filename in os.listdir(args.cache):
        pathname = os.path.join(args.cache, filename)
        if os.path.getmtime(pathname) < cutoffdate:
            os.remove(pathname)

if findings:
    print("Findings in %s file(s):" % len(findings))
    for finding in findings:
        print("  %s" % finding)
    sys.exit(1)
