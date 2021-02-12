#!/usr/bin/env python3

import argparse
import hashlib
import json
import multiprocessing
import os
import queue
import random
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


disabled_checks = [
    # aliases for other checks
    "bugprone-narrowing-conversions",
    "cert-dcl03-c",
    "cert-dcl16-c",
    "cert-dcl54-cpp",
    "cert-dcl59-cpp",
    "cert-err61-cpp",
    "cert-fio38-c",
    "cert-msc30-c",
    "cert-msc32-c",
    "cert-oop11-cpp",
    "cert-oop54-cpp",
    "cert-pos44-c",
    "cppcoreguidelines-avoid-c-arrays",
    "cppcoreguidelines-avoid-magic-numbers",
    "cppcoreguidelines-c-copy-assignment-signature",
    "cppcoreguidelines-explicit-virtual-functions",
    "cppcoreguidelines-non-private-member-variables-in-classes",
    "fuchsia-header-anon-namespaces",
    "google-readability-braces-around-statements",
    "google-readability-function-size",
    "google-readability-namespace-comments",
    "hicpp-avoid-c-arrays",
    "hicpp-avoid-goto",
    "hicpp-braces-around-statements",
    "hicpp-deprecated-headers",
    "hicpp-explicit-conversions",
    "hicpp-function-size",
    "hicpp-invalid-access-moved",
    "hicpp-member-init",
    "hicpp-move-const-arg",
    "hicpp-named-parameter",
    "hicpp-new-delete-operators",
    "hicpp-no-array-decay",
    "hicpp-no-malloc",
    "hicpp-special-member-functions",
    "hicpp-static-assert",
    "hicpp-uppercase-literal-suffix",
    "hicpp-use-auto",
    "hicpp-use-emplace",
    "hicpp-use-equals-default",
    "hicpp-use-equals-delete",
    "hicpp-use-noexcept",
    "hicpp-use-nullptr",
    "hicpp-use-override",
    "hicpp-vararg",
    "llvm-qualified-auto",

    # only relevant for c++
    "bugprone-copy-constructor-init",
    "bugprone-dangling-handle",
    "bugprone-exception-escape",
    "bugprone-fold-init-type",
    "bugprone-forward-declaration-namespace",
    "bugprone-forwarding-reference-overload",
    "bugprone-inaccurate-erase",
    "bugprone-lambda-function-name",
    "bugprone-move-forwarding-reference",
    "bugprone-parent-virtual-call",
    "bugprone-sizeof-container",
    "bugprone-string-constructor",
    "bugprone-string-integer-assignment",
    "bugprone-swapped-arguments",
    "bugprone-throw-keyword-missing",
    "bugprone-undelegated-constructor",
    "bugprone-unhandled-self-assignment",
    "bugprone-unused-raii",
    "bugprone-unused-return-value",
    "bugprone-use-after-move",
    "bugprone-virtual-near-miss",
    "cert-dcl21-cpp",
    "cert-dcl50-cpp",
    "cert-dcl58-cpp",
    "cert-err09-cpp",
    "cert-err52-cpp",
    "cert-err58-cpp",
    "cert-err60-cpp",
    "cert-mem57-cpp",
    "cert-msc50-cpp",
    "cert-msc51-cpp",
    "cert-oop58-cpp",
    "clang-analyzer-cplusplus.InnerPointer",
    "clang-analyzer-cplusplus.Move",
    "clang-analyzer-cplusplus.NewDelete",
    "clang-analyzer-cplusplus.NewDeleteLeaks",
    "clang-analyzer-cplusplus.PureVirtualCall",
    "clang-analyzer-cplusplus.SelfAssignment",
    "clang-analyzer-cplusplus.SmartPtr",
    "clang-analyzer-cplusplus.VirtualCallModeling",
    "clang-analyzer-optin.cplusplus.UninitializedObject",
    "clang-analyzer-optin.cplusplus.VirtualCall",
    "cppcoreguidelines-no-malloc",
    "cppcoreguidelines-owning-memory",
    "cppcoreguidelines-pro-bounds-array-to-pointer-decay",
    "cppcoreguidelines-pro-bounds-constant-array-index",
    "cppcoreguidelines-pro-bounds-pointer-arithmetic",
    "cppcoreguidelines-pro-type-const-cast",
    "cppcoreguidelines-pro-type-cstyle-cast",
    "cppcoreguidelines-pro-type-member-init",
    "cppcoreguidelines-pro-type-reinterpret-cast",
    "cppcoreguidelines-pro-type-static-cast-downcast",
    "cppcoreguidelines-pro-type-union-access",
    "cppcoreguidelines-pro-type-vararg",
    "cppcoreguidelines-slicing",
    "cppcoreguidelines-special-member-functions",
    "fuchsia-default-arguments-calls",
    "fuchsia-default-arguments-declarations",
    "fuchsia-multiple-inheritance",
    "fuchsia-overloaded-operator",
    "fuchsia-statically-constructed-objects",
    "fuchsia-trailing-return",
    "fuchsia-virtual-inheritance",
    "google-build-explicit-make-pair",
    "google-build-namespaces",
    "google-build-using-namespace",
    "google-default-arguments",
    "google-explicit-constructor",
    "google-global-names-in-headers",
    "google-readability-casting",
    "google-runtime-operator",
    "google-runtime-references",
    "hicpp-exception-baseclass",
    "hicpp-noexcept-move",
    "hicpp-undelegated-constructor",
    "llvm-namespace-comment",
    "llvm-prefer-isa-or-dyn-cast-in-conditionals",
    "llvm-prefer-register-over-unsigned",
    "llvm-twine-local",
    "misc-new-delete-overloads",
    "misc-non-private-member-variables-in-classes",
    "misc-throw-by-value-catch-by-reference",
    "misc-unconventional-assign-operator",
    "misc-uniqueptr-reset-release",
    "misc-unused-using-decls",
    "modernize-avoid-bind",
    "modernize-avoid-c-arrays",
    "modernize-concat-nested-namespaces",
    "modernize-deprecated-headers",
    "modernize-deprecated-ios-base-aliases",
    "modernize-loop-convert",
    "modernize-make-shared",
    "modernize-make-unique",
    "modernize-pass-by-value",
    "modernize-raw-string-literal",
    "modernize-redundant-void-arg",
    "modernize-replace-auto-ptr",
    "modernize-replace-random-shuffle",
    "modernize-return-braced-init-list",
    "modernize-shrink-to-fit",
    "modernize-unary-static-assert",
    "modernize-use-auto",
    "modernize-use-bool-literals",
    "modernize-use-default-member-init",
    "modernize-use-emplace",
    "modernize-use-equals-default",
    "modernize-use-equals-delete",
    "modernize-use-nodiscard",
    "modernize-use-noexcept",
    "modernize-use-nullptr",
    "modernize-use-override",
    "modernize-use-trailing-return-type",
    "modernize-use-transparent-functors",
    "modernize-use-uncaught-exceptions",
    "modernize-use-using",
    "performance-faster-string-find",
    "performance-for-range-copy",
    "performance-implicit-conversion-in-loop",
    "performance-inefficient-algorithm",
    "performance-inefficient-string-concatenation",
    "performance-inefficient-vector-operation",
    "performance-move-const-arg",
    "performance-move-constructor-init",
    "performance-no-automatic-move",
    "performance-noexcept-move-constructor",
    "performance-trivially-destructible",
    "performance-type-promotion-in-math-fn",
    "performance-unnecessary-copy-initialization",
    "performance-unnecessary-value-param",
    "portability-simd-intrinsics",
    "readability-container-size-empty",
    "readability-convert-member-functions-to-static",
    "readability-deleted-default",
    "readability-make-member-function-const",
    "readability-qualified-auto",
    "readability-redundant-access-specifiers",
    "readability-redundant-member-init",
    "readability-redundant-smartptr-get",
    "readability-redundant-string-cstr",
    "readability-redundant-string-init",
    "readability-simplify-subscript-expr",
    "readability-static-accessed-through-instance",
    "readability-static-definition-in-anonymous-namespace",
    "readability-string-compare",
    "readability-uniqueptr-delete-release",
    "zircon-temporary-objects",

    # only relevant for objective c
    "clang-analyzer-nullability.NullableDereferenced",
    "clang-analyzer-nullability.NullablePassedToNonnull",
    "clang-analyzer-nullability.NullableReturnedFromNonnull",
    "clang-analyzer-nullability.NullPassedToNonnull",
    "clang-analyzer-nullability.NullReturnedFromNonnull",
    "clang-analyzer-optin.osx.cocoa.localizability." +
    "EmptyLocalizationContextChecker",
    "clang-analyzer-optin.osx.cocoa.localizability." +
    "NonLocalizedStringChecker",
    "clang-analyzer-optin.osx.OSObjectCStyleCast",
    "clang-analyzer-optin.performance.GCDAntipattern",
    "google-objc-avoid-nsobject-new",
    "google-objc-avoid-throwing-exception",
    "google-objc-function-naming",
    "google-objc-global-variable-declaration",
    "objc-avoid-nserror-init",
    "objc-forbidden-subclassing",
    "objc-missing-hash",
    "objc-property-declaration",
    "objc-super-self",

    # only relevant for certain libraries
    "abseil-duration-addition",
    "abseil-duration-comparison",
    "abseil-duration-conversion-cast",
    "abseil-duration-division",
    "abseil-duration-factory-float",
    "abseil-duration-factory-scale",
    "abseil-duration-subtraction",
    "abseil-duration-unnecessary-conversion",
    "abseil-faster-strsplit-delimiter",
    "abseil-no-internal-dependencies",
    "abseil-no-namespace",
    "abseil-redundant-strcat-calls",
    "abseil-str-cat-append",
    "abseil-string-find-startswith",
    "abseil-time-comparison",
    "abseil-time-subtraction",
    "abseil-upgrade-duration-conversions",
    "boost-use-to-string",
    "clang-analyzer-optin.mpi.MPI-Checker",
    "google-readability-avoid-underscore-in-googletest-name",
    "google-upgrade-googletest-case",
    "mpi-buffer-deref",
    "mpi-type-mismatch",
    "openmp-exception-escape",
    "openmp-use-default-none",

    # only relevant for osx
    "clang-analyzer-osx.API",
    "clang-analyzer-osx.cocoa.AtSync",
    "clang-analyzer-osx.cocoa.AutoreleaseWrite",
    "clang-analyzer-osx.cocoa.ClassRelease",
    "clang-analyzer-osx.cocoa.Dealloc",
    "clang-analyzer-osx.cocoa.IncompatibleMethodTypes",
    "clang-analyzer-osx.cocoa.Loops",
    "clang-analyzer-osx.cocoa.MissingSuperCall",
    "clang-analyzer-osx.cocoa.NilArg",
    "clang-analyzer-osx.cocoa.NonNilReturnValue",
    "clang-analyzer-osx.cocoa.NSAutoreleasePool",
    "clang-analyzer-osx.cocoa.NSError",
    "clang-analyzer-osx.cocoa.ObjCGenerics",
    "clang-analyzer-osx.cocoa.RetainCount",
    "clang-analyzer-osx.cocoa.RetainCountBase",
    "clang-analyzer-osx.cocoa.RunLoopAutoreleaseLeak",
    "clang-analyzer-osx.cocoa.SelfInit",
    "clang-analyzer-osx.cocoa.SuperDealloc",
    "clang-analyzer-osx.cocoa.UnusedIvars",
    "clang-analyzer-osx.cocoa.VariadicMethodTypes",
    "clang-analyzer-osx.coreFoundation.CFError",
    "clang-analyzer-osx.coreFoundation.CFNumber",
    "clang-analyzer-osx.coreFoundation.CFRetainRelease",
    "clang-analyzer-osx.coreFoundation.containers.OutOfBounds",
    "clang-analyzer-osx.coreFoundation.containers.PointerSizedValues",
    "clang-analyzer-osx.MIG",
    "clang-analyzer-osx.NSOrCFErrorDerefChecker",
    "clang-analyzer-osx.NumberObjectConversion",
    "clang-analyzer-osx.ObjCProperty",
    "clang-analyzer-osx.OSObjectRetainCount",
    "clang-analyzer-osx.SecKeychainAPI",
    "darwin-avoid-spinlock",
    "darwin-dispatch-once-nonstatic",

    # failing checks
    "android-cloexec-dup",
    "android-cloexec-fopen",
    "android-cloexec-open",
    "android-cloexec-pipe",
    "bugprone-branch-clone",
    "bugprone-integer-division",
    "bugprone-macro-parentheses",
    "bugprone-signed-char-misuse",
    "bugprone-sizeof-expression",
    "bugprone-suspicious-missing-comma",
    "bugprone-suspicious-string-compare",
    "cert-err34-c",
    "clang-analyzer-core.CallAndMessage",
    "clang-analyzer-core.NonNullParamChecker",
    "clang-analyzer-core.NullDereference",
    "clang-analyzer-core.UndefinedBinaryOperatorResult",
    "clang-analyzer-core.uninitialized.Branch",
    "clang-analyzer-deadcode.DeadStores",
    "clang-analyzer-optin.performance.Padding",
    "clang-analyzer-security.insecureAPI.strcpy",
    "clang-analyzer-unix.Malloc",
    "cppcoreguidelines-init-variables",
    "cppcoreguidelines-interfaces-global-init",
    "cppcoreguidelines-narrowing-conversions",
    "hicpp-multiway-paths-covered",
    "hicpp-no-assembler",
    "hicpp-signed-bitwise",
    "llvm-include-order",
    "readability-braces-around-statements",
    "readability-else-after-return",
    "readability-function-size",
    "readability-inconsistent-declaration-parameter-name",
    "readability-isolate-declaration",
    "readability-magic-numbers",
    "readability-misleading-indentation",
    "readability-named-parameter",
    "readability-non-const-parameter",
    "readability-redundant-control-flow",
    "readability-redundant-declaration",
    "readability-uppercase-literal-suffix",
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
    parser.add_argument(
        "--timeout",
        dest="timeout",
        type=int,
        help="Timeout in minutes")
    parser.add_argument(
        "--allow-timeout",
        dest="allow_timeout",
        action="store_true",
        help="Do not treat timeout as failure if set")
    parser.add_argument(
        "--shuffle-input",
        dest="shuffle_input",
        action="store_true",
        help="Randomize order of files to check")

    return parser.parse_args()


def run_clang_tidy(item):
    cmd = (
        "clang-tidy",
        "--warnings-as-errors=*",
        "--checks=-*,%s" % ",".join(checks),
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


def cache_name(item, checks):
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
    hashsum.update("\n".join(checks).encode())
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


def worker(checks):
    while True:
        item = items.get()
        if args.timeout and args.timeout < time.time():
            if not args.allow_timeout:
                findings.append("%s (timeout)" % item["file"])
            items.task_done()
            continue

        os.chdir(item["directory"])

        cache = cache_name(item, checks)
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


def list_checks():
    output = subprocess.check_output(
        ["clang-tidy", "-checks=*", "-list-checks"],
        universal_newlines=True).split("\n")[1:]

    output = [line.strip() for line in output]
    output = [line for line in output if line not in disabled_checks]
    return output


args = parse_args()
items = queue.Queue()
lock = threading.Lock()
checks = list_checks()
findings = list()

if args.cache:
    args.cache = os.path.abspath(args.cache)
    os.makedirs(args.cache, exist_ok=True)

if args.timeout:
    args.timeout = time.time() + args.timeout * 60

print("Enabled checks:")
for check in checks:
    print("    %s" % check)

for _ in range(args.thread_num):
    threading.Thread(target=worker, daemon=True, args=[checks]).start()

with open(os.path.join(args.build_dir, "compile_commands.json")) as f:
    compile_commands = json.load(f)
    if args.shuffle_input:
        random.shuffle(compile_commands)
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
