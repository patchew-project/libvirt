#!/usr/bin/env python3
#
# Copyright (C) 2020 Shandong Massclouds Co.,Ltd.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library.  If not, see
# <http://www.gnu.org/licenses/>.
#

import os
import re
import sys
import argparse
from clang.cindex import Config, Index, CursorKind
from clang.cindex import SourceLocation, SourceRange, TokenKind
from datetime import datetime
from directive import TypeTable, showDirective
from directive import makeClearFunc, makeParseFunc, makeFormatFunc
from utils import Terms

TOOL_DESC = '''
Generate xml parse/format functions based on directives.

Subcommand:\n
  list: List types. By default, only list structs tagged by
        'genparse'/'genformat'. When the option '-a' is specified,
        list all types discovered by this tool.\n
  show: Show the target type's directives and its code for preview.
        Specify target type by its name. The option '-k' indicates
        the kinds of code for preview.\n
  generate: Generate code. To be called by Makefile.
        It needs option -k to filter output.

Option:\n
  -k: Specify kinds to filter code output. More than one kind can be
      specified, 'c' for clearfunc; 'p' for parsefunc;
      'f' for formatfunc.\n
  The option '-k' is only valid for show and generate.
'''

# Three builtin types need to be handled specially:
# 'char *' => String
# 'char XXXX[...]' => Chars
# 'unsigned char XXXX[...]' => UChars
BUILTIN_MAP = {
    'bool': 'Bool',
    'char': 'Char',
    'unsigned char': 'UChar',
    'int': 'Int',
    'unsigned int': 'UInt',
    'long': 'Long',
    'unsigned long': 'ULong',
    'long long': 'LongLong',
    'unsigned long long': 'ULongLong',
    'uint8_t': 'U8',
    'uint32_t': 'U32',
}


def getBuiltinType(ctype, ptr=False, size=None):
    if ctype == 'char':
        if ptr:
            return 'String'
        elif size:
            return 'Chars'

    if ctype == 'unsigned char' and size:
        return 'UChars'

    return BUILTIN_MAP.get(ctype, None)


def cursorLineExtent(cursor, tu):
    loc = cursor.location
    start = SourceLocation.from_position(tu, loc.file, loc.line, 1)
    end = SourceLocation.from_position(tu, loc.file, loc.line, -1)
    return SourceRange.from_locations(start, end)


def getTokens(cursor, tu):
    return tu.get_tokens(extent=cursorLineExtent(cursor, tu))


DIRECTIVES = [
    'genparse', 'genformat', 'namespace', 'xmlattr', 'xmlelem',
    'required', 'array', 'specified', 'callback', 'truevalue', 'checkformat'
]


def createDirectives(text):
    tlist = re.findall(r'/\*(.*)\*/', text)
    if len(tlist) != 1:
        return None

    tlist = tlist[0].split(',')
    if len(tlist) == 0:
        return None

    directives = {}
    for item in tlist:
        item = item.strip()
        if ':' in item:
            key, value = item.split(':')
        else:
            key, value = item, None

        if key in DIRECTIVES:
            directives[key] = value
    return directives


def getDirectives(tokens, cursor):
    for token in tokens:
        if token.location.column <= cursor.location.column:
            continue
        if token.kind == TokenKind.COMMENT:
            directive = createDirectives(token.spelling)
            if directive:
                return directive
    return None


def determinType(kvs, tokens, cursor):
    prefix = []
    kind = None
    for token in tokens:
        if token.location.column >= cursor.location.column:
            break
        if not kind:
            kind = token.kind
        prefix.append(token.spelling)

    suffix = []
    for token in tokens:
        if token.spelling == ';':
            break
        suffix.append(token.spelling)

    size = None
    if len(suffix) == 3 and suffix[0] == '[' and suffix[2] == ']':
        size = suffix[1]

    assert kind in [TokenKind.IDENTIFIER, TokenKind.KEYWORD], \
        'Bad field "%s".' % cursor.spelling

    assert prefix
    typename = ' '.join(prefix)

    # For array, remove the most-outer pointer
    if kvs.get('array'):
        if typename.endswith('Ptr'):
            typename = typename[:-3]
        elif typename.endswith('*'):
            typename = typename[:-1].strip()

    ptr = False
    if typename.endswith('Ptr'):
        typename = typename[:-3]
        ptr = True
    elif prefix[-1] == '*':
        typename = typename[:-1].strip()
        ptr = True

    ret = getBuiltinType(typename, ptr, size)
    if ret:
        typename = ret

    kvs.update({'type': typename, 'pointer': ptr})
    if size:
        kvs['size'] = size
    return kvs


def analyseMember(cursor, tu):
    dvs = getDirectives(getTokens(cursor, tu), cursor)
    if not dvs:
        return None

    kvs = {'name': cursor.spelling}
    kvs.update(dvs)

    # Formalize member
    for key in ['array', 'required', 'specified']:
        if key in kvs:
            kvs[key] = True

    if 'checkformat' in kvs:
        assert kvs.get('checkformat'), 'Directive "checkformat" is None'

    for tag in ['xmlattr', 'xmlelem']:
        if tag in kvs:
            if not kvs[tag]:
                if kvs.get('array'):
                    kvs[tag] = Terms.singularize(kvs['name'])
                else:
                    kvs[tag] = kvs['name']

    return determinType(kvs, getTokens(cursor, tu), cursor)


def analyseStruct(struct, cursor, tu):
    tokens = getTokens(cursor, tu)
    kvs = getDirectives(tokens, cursor)
    if kvs:
        path, _ = os.path.splitext(cursor.location.file.name)
        path, filename = os.path.split(path)
        _, dirname = os.path.split(path)
        kvs['output'] = dirname + '/' + filename
        struct.update(kvs)

        inner_members = []
        for child in cursor.get_children():
            if inner_members:
                # Flatten the members of embedded struct
                for member in inner_members:
                    member['name'] = child.spelling + '.' + member['name']
                    struct['members'].append(member)
                inner_members = []
                continue

            if child.kind == CursorKind.STRUCT_DECL:
                for ichild in child.get_children():
                    member = analyseMember(ichild, tu)
                    if member:
                        inner_members.append(member)
                continue

            member = analyseMember(child, tu)
            if member:
                struct['members'].append(member)

    return struct


def discoverStructures(tu):
    for cursor in tu.cursor.get_children():
        if cursor.kind == CursorKind.STRUCT_DECL and cursor.is_definition():
            # Detect structs
            name = cursor.spelling
            if not name:
                continue
            if name.startswith('_'):
                name = name[1:]
            struct = {'name': name, 'meta': 'Struct', 'members': []}
            analyseStruct(struct, cursor, tu)
            TypeTable().register(struct)
        elif cursor.kind == CursorKind.TYPEDEF_DECL:
            # Detect enums
            # We can't seek out enums by CursorKind.ENUM_DECL,
            # since almost all enums are anonymous.
            token = cursor.get_tokens()
            try:
                next(token)     # skip 'typedef'
                if next(token).spelling == 'enum':
                    enum = {'name': cursor.spelling, 'meta': 'Enum'}
                    TypeTable().register(enum)
            except StopIteration:
                pass


class CodeWriter(object):
    def __init__(self, args, builddir):
        self._builddir = builddir
        self._cmd = args.cmd
        self._files = {}
        self._filters = {}
        self._filters['clearfunc'] = args.kinds and 'c' in args.kinds
        self._filters['parsefunc'] = args.kinds and 'p' in args.kinds
        self._filters['formatfunc'] = args.kinds and 'f' in args.kinds
        if args.cmd == 'show':
            self._filters['target'] = args.target

    def _getFile(self, path, ext):
        assert ext in ['.h', '.c']
        _, basename = os.path.split(path)
        path = '%s.generated%s' % (path, ext)
        f = self._files.get(path)
        if f is None:
            f = open(path, 'w')
            f.write('/* Generated by build-aux/generator */\n\n')
            if ext in ['.c']:
                f.write('#include <config.h>\n')
                f.write('#include "%s.h"\n' % basename)
                f.write('#include "viralloc.h"\n')
                f.write('#include "virerror.h"\n')
                f.write('#include "virstring.h"\n\n')
                f.write('#define VIR_FROM_THIS VIR_FROM_NONE\n')
            else:
                f.write('#pragma once\n\n')
                f.write('#include "internal.h"\n')
                f.write('#include "virxml.h"\n')
            self._files[path] = f
        return f

    def write(self, atype, kind, extname, content):
        if not self._filters[kind]:
            return

        if self._cmd == 'show':
            target = self._filters['target']
            if not target or target == atype['name']:
                if extname == '.h':
                    info = Terms.upperInitial(kind)
                    print('\n###### %s ######' % info)
                    print('\n[.h]')
                else:
                    print('\n[.c]')
                print('\n' + content)
            return

        assert self._cmd == 'generate'

        if atype.get('output'):
            lfs = '\n' if extname == '.h' else '\n\n'
            path = self._builddir + '/src/' + atype['output']
            f = self._getFile(path, extname)
            f.write(lfs + content + '\n')

    def complete(self):
        for name in self._files:
            self._files[name].close()
        self._files.clear()


def getHFiles(path):
    retlist = []
    for fname in os.listdir(path):
        if fname.endswith('.h'):
            retlist.append(os.path.join(path, fname))
    return retlist


HELP_LIST = 'list structs tagged by "genparse"/"genformat"'
HELP_LIST_ALL = 'list all discovered types'

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=TOOL_DESC)
    subparsers = parser.add_subparsers(dest='cmd')
    parser_list = subparsers.add_parser('list', help=HELP_LIST)
    parser_list.add_argument('-a', dest='list_all', action='store_true',
                             default=False, help=HELP_LIST_ALL)
    parser_show = subparsers.add_parser('show', help='show target code')
    parser_show.add_argument('target', help='target for being previewed')
    parser_show.add_argument('-k', dest='kinds',
                             help='kinds of code to be previewed')
    parser_generate = subparsers.add_parser('generate', help='generate code')
    parser_generate.add_argument('-k', dest='kinds',
                                 help='kinds of code to be generated')
    args = parser.parse_args()

    if not args.cmd:
        parser.print_help()
        sys.exit(1)

    if args.cmd == 'generate':
        print('###### Generator: start ... ######')
        if not args.kinds:
            print("[dry run]: no kinds specified for 'generate'")

    timestamp = datetime.now()
    topdir = os.environ.get('topdir', None)
    builddir = os.environ.get('builddir', None)
    assert topdir and builddir, 'Set env "topdir" and "builddir".'

    libclang_path = os.environ.get('libclang_path')
    assert libclang_path, 'No libclang library.'
    Config.set_library_file(libclang_path)

    # Examine all *.h in "$(topdir)/src/[util|conf]"
    index = Index.create()
    hfiles = getHFiles(topdir + '/src/util') + getHFiles(topdir + '/src/conf')
    for hfile in hfiles:
        tu = index.parse(hfile)
        discoverStructures(tu)  # find all structs and enums

    if args.cmd == 'list':
        print('%-64s %s' % ('TYPENAME', 'META'))
        for name, kvs in TypeTable().items():
            if not args.list_all:
                if not ('genparse' in kvs or 'genparse' in kvs):
                    continue
            print('%-64s %s' % (name, kvs['meta']))
        sys.exit(0)
    elif args.cmd == 'show':
        assert args.target, args
        atype = TypeTable().get(args.target)
        if not atype:
            sys.exit(0)
        showDirective(atype)

    writer = CodeWriter(args, builddir)

    for atype in TypeTable().values():
        makeClearFunc(writer, atype)
        makeParseFunc(writer, atype)
        makeFormatFunc(writer, atype)

    writer.complete()

    if args.cmd == 'generate':
        elapse = (datetime.now() - timestamp).microseconds
        print('\n###### Generator: elapse %d(us) ######\n' % elapse)

    sys.exit(0)
