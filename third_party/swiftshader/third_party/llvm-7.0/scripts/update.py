#!/usr/bin/env python3

# Copyright 2018 The SwiftShader Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from subprocess import run

import argparse
import multiprocessing
import os
import re
import shutil


LLVM_DIR = os.path.abspath(os.path.join('..', 'llvm'))

LLVM_CONFIGS = os.path.abspath(os.path.join('..', 'configs'))

LLVM_OBJS = os.path.join(os.getcwd(), 'llvm_objs')

LLVM_TARGETS = [
    ('AArch64', ('__aarch64__',)),
    ('ARM', ('__arm__',)),
    ('X86', ('__i386__', '__x86_64__')),
]

LLVM_TRIPLES = {
    'android': [
        ('__x86_64__', 'x86_64-linux-android'),
        ('__i386__', 'i686-linux-android'),
        ('__arm__', 'armv7-linux-androideabi'),
        ('__aarch64__', 'aarch64-linux-android'),
    ],
    'linux': [
        ('__x86_64__', 'x86_64-unknown-linux-gnu'),
        ('__i386__', 'i686-pc-linux-gnu'),
        ('__arm__', 'armv7-linux-gnueabihf'),
        ('__aarch64__', 'aarch64-linux-gnu'),
    ],
}

LLVM_OPTIONS = [
    '-DCMAKE_BUILD_TYPE=Release',
    '-DLLVM_TARGETS_TO_BUILD=' + ';'.join(t[0] for t in LLVM_TARGETS),
    '-DLLVM_ENABLE_TERMINFO=OFF',
    '-DLLVM_ENABLE_LIBXML2=OFF',
    '-DLLVM_ENABLE_LIBEDIT=OFF',
    '-DLLVM_ENABLE_LIBPFM=OFF',
    '-DLLVM_ENABLE_ZLIB=OFF',
]


def _parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('name', help='destination name',
                        choices=['android', 'linux'])
    parser.add_argument('-j', '--jobs', help='parallel compilation', type=int)
    return parser.parse_args()


def build_llvm(num_jobs):
    """Build LLVM and get all generated files."""
    if num_jobs is None:
        num_jobs = multiprocessing.cpu_count()

    os.makedirs(LLVM_OBJS, exist_ok=True)
    run(['cmake', LLVM_DIR] + LLVM_OPTIONS, cwd=LLVM_OBJS)
    run(['make', '-j' + str(num_jobs)], cwd=LLVM_OBJS)


def list_files(src_base, src, dst_base, suffixes):
    """Enumerate the files that are under `src` and end with one of the
    `suffixes` and yield the source path and the destination path."""
    src_base = os.path.abspath(src_base)
    src = os.path.join(src_base, src)
    for base_dir, dirnames, filenames in os.walk(src):
        for filename in filenames:
            if os.path.splitext(filename)[1] in suffixes:
                relative = os.path.relpath(base_dir, src_base)
                yield (os.path.join(base_dir, filename),
                       os.path.join(dst_base, relative, filename))


def copy_common_generated_files(dst_base):
    """Copy platform-independent generated files."""
    suffixes = {'.inc', '.h', '.def'}
    subdirs = [
        os.path.join('include', 'llvm', 'IR'),
        os.path.join('include', 'llvm', 'Support'),
        os.path.join('lib', 'IR'),
        os.path.join('lib', 'Target', 'AArch64'),
        os.path.join('lib', 'Target', 'ARM'),
        os.path.join('lib', 'Target', 'X86'),
        os.path.join('lib', 'Transforms', 'InstCombine'),
    ]
    for subdir in subdirs:
        for src, dst in list_files(LLVM_OBJS, subdir, dst_base, suffixes):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)


def copy_platform_file(platform, src, dst):
    """Copy platform-dependent generated files and add platform-specific
    modifications."""

    # LLVM configuration patterns to be post-processed.
    llvm_target_pattern = re.compile('^LLVM_[A-Z_]+\\(([A-Za-z0-9_]+)\\)$')
    llvm_native_pattern = re.compile(
        '^#define LLVM_NATIVE_([A-Z]+) (LLVMInitialize)?(.*)$')
    llvm_triple_pattern = re.compile('^#define (LLVM_[A-Z_]+_TRIPLE) "(.*)"$')
    llvm_define_pattern = re.compile('^#define ([A-Za-z0-9_]+) (.*)$')

    # LLVM configurations to be undefined.
    undef_names = [
        'BACKTRACE_HEADER',
        'ENABLE_BACKTRACES',
        'ENABLE_CRASH_OVERRIDES',
        'HAVE_BACKTRACE',
        'HAVE_POSIX_SPAWN',
        'HAVE_PTHREAD_GETNAME_NP',
        'HAVE_PTHREAD_SETNAME_NP',
        'HAVE_TERMIOS_H',
        'HAVE_ZLIB_H',
        'HAVE__UNWIND_BACKTRACE',
    ]

    # Build architecture-specific conditions.
    conds = {}
    for arch, defs in LLVM_TARGETS:
        conds[arch] = ' || '.join('defined(' + v + ')' for v in defs)

    # Get a set of platform-specific triples.
    triples = LLVM_TRIPLES[platform]

    with open(src, 'r') as src_file:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'w') as dst_file:
            for line in src_file:
                match = llvm_target_pattern.match(line)
                if match:
                    arch = match.group(1)
                    print('#if ' + conds[arch], file=dst_file)
                    print(line, file=dst_file, end='')
                    print('#endif', file=dst_file)
                    continue

                match = llvm_native_pattern.match(line)
                if match:
                    name = match.group(1)
                    init = match.group(2) or ''
                    arch = match.group(3)
                    end = ''
                    if arch.lower().endswith(name.lower()):
                        end = arch[-len(name):]
                    directive = '#if '
                    for arch, defs in LLVM_TARGETS:
                        print(directive + conds[arch], file=dst_file)
                        print('#define LLVM_NATIVE_' + name + ' ' +
                              init + arch + end, file=dst_file)
                        directive = '#elif '
                    print('#else', file=dst_file)
                    print('#error "unknown architecture"', file=dst_file)
                    print('#endif', file=dst_file)
                    continue

                match = llvm_triple_pattern.match(line)
                if match:
                    name = match.group(1)
                    directive = '#if'
                    for defs, triple in triples:
                        print(directive + ' defined(' + defs + ')',
                              file=dst_file)
                        print('#define ' + name + ' "' + triple + '"',
                              file=dst_file)
                        directive = '#elif'
                    print('#else', file=dst_file)
                    print('#error "unknown architecture"', file=dst_file)
                    print('#endif', file=dst_file)
                    continue

                match = llvm_define_pattern.match(line)
                if match and match.group(1) in undef_names:
                    print('/* #undef ' + match.group(1) + ' */', file=dst_file)
                    continue

                print(line, file=dst_file, end='')


def copy_platform_generated_files(platform, dst_base):
    """Copy platform-specific generated files."""
    suffixes = {'.inc', '.h', '.def'}
    src_dir = os.path.join('include', 'llvm', 'Config')
    for src, dst in list_files(LLVM_OBJS, src_dir, dst_base, suffixes):
        copy_platform_file(platform, src, dst)


def main():
    args = _parse_args()
    build_llvm(args.jobs)
    copy_common_generated_files(os.path.join(LLVM_CONFIGS, 'common'))
    copy_platform_generated_files(
        args.name, os.path.join(LLVM_CONFIGS, args.name))


if __name__ == '__main__':
    main()
