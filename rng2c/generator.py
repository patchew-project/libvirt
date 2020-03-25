#!/usr/bin/env python
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
# References:
#   http://relaxng.org/spec-20011203.html
#   https://www.w3.org/TR/xmlschema-2/#decimal
#

import os
import sys
import json
import argparse
from copy import deepcopy
from datetime import datetime
from xml.dom import minidom

from utils import assertOnlyOne, assertObj, Terms, deepupdate
from directive import TypeTable, NodeList, createMember
from directive import initDirectiveSchema, makeStructure
from directive import makeClearFunc, makeParseFunc, makeFormatFunc
from directive import verifyType, showDirective, isBuiltin, BUILTIN_TYPES

g_rngs = []
g_defines = {}
g_touches = []

VIRT_DIRECTIVE_HEAD = 'VIRT:DIRECTIVE'

TOOL_DESC = '''
Generate c-language code based on relax-ng files.

Subcommand:\n
  list: List all types discovered by this tool. Display three fields
        SHORTID/MEAT/LOCATION for each type, SHORTID is the type's
        sha1-id, only reserve the leftmost 8 digits; META is the
        type's meta, includes 'Struct', 'Enum', 'String', 'UInt',
        'Bool', etc.; LOCATION indicates where the type derives from.\n
  show: Show the target type's directives and its code for preview.
        Specify target type by its SHORTID or LOCATION. The option
        '-k' indicates the kinds of code for preview.\n
  generate: Generate code. To be called by Makefile.
        It needs option -k to filter output.

Option:\n
  -k: Specify kinds to filter code output. More than one kind can be
      specified, 's' for structure; 'c' for clearfunc; 'p' for parsefunc;
      'f' for formatfunc.\n
  The option '-k' is only valid for show and generate.
'''


def _getText(xnode):
    return assertOnlyOne(xnode.childNodes).data


def _resetParentNames(kvs, name):
    kvs['id'] = name
    kvs['_nodepath'].append((name, '.' + kvs['tag']))


def collectJson(target, line, parent):
    kvs = json.loads(line)
    if 'PRESERVE' in kvs:
        key = kvs.pop('PRESERVE')
        kvs['_anchor'] = len(parent.get('_nodepath', []))
        target.setdefault('_preserve_table', {})
        target['_preserve_table'][key] = kvs
        target['_preserve'] = key
    elif 'APPLY' in kvs:
        key = kvs.pop('APPLY')
        kvs = parent['_preserve_table'][key]
        target.update(deepcopy(kvs))
    else:
        target.update(kvs)


def _makekvs(xnode, directives, parentkvs):
    name = xnode.getAttribute('name')
    kvs = {'id': name, 'tag': xnode.tagName,
           '_env': {}, '_nodepath': [], '_preserve_table': {}}
    kvs['_env'].update(parentkvs['_env'])
    kvs['_preserve_table'].update(parentkvs.get('_preserve_table', {}))
    kvs['_nodepath'].extend(parentkvs.get('_nodepath', []))
    if xnode.tagName == 'choice':
        kvs['_nodepath'].append(('', 'choice'))
    elif xnode.tagName == 'data':
        kvs['_nodepath'].append((xnode.getAttribute('type'), '.data'))
    elif name:
        kvs['_nodepath'].append((name, '.' + xnode.tagName))
    return deepupdate(kvs, directives)


def _traverse(xchildren, parentkvs):
    directive = {}
    nodes = NodeList()
    for xchild in xchildren:
        if xchild.nodeType is xchild.COMMENT_NODE:
            line = xchild.data.strip()
            if line.startswith(VIRT_DIRECTIVE_HEAD):
                collectJson(directive, line[len(VIRT_DIRECTIVE_HEAD):],
                            parentkvs)
        elif xchild.nodeType is xchild.ELEMENT_NODE:
            if xchild.getAttribute('ns'):
                continue
            if not verifyType(directive):
                sys.exit(-1)
            childkvs = _makekvs(xchild, directive, parentkvs)
            directive = {}
            opFunc = globals().get('op%s' % Terms.camelize(xchild.tagName))
            assert opFunc, "Unsupported tag '%s'" % xchild.tagName
            nodes.extend(opFunc(childkvs, parentkvs, xchild))
    return nodes


def opGrammar(kvs, parentkvs, _):
    global g_touches
    global g_rngs
    g_rngs.append(kvs['_env']['rng'])
    path = kvs['_env']['topdir'] + '/docs/schemas/' + kvs['_env']['rng']
    doc = minidom.parse(path)
    grammar = doc.getElementsByTagName('grammar')[0]
    _traverse(grammar.childNodes, kvs)
    if 'start.xnode' in kvs:
        for touch in g_touches:
            opRef({'id': touch}, {}, None)
        g_touches = []
        _traverse(kvs['start.xnode'].childNodes, kvs['start.kvs'])
    return None


def opStart(kvs, parentkvs, xnode):
    parentkvs['start.xnode'] = xnode
    parentkvs['start.kvs'] = kvs
    return None


def opInclude(kvs, parentkvs, xnode):
    global g_rngs
    rng = xnode.getAttribute('href')
    if rng not in g_rngs:
        kvs['_env']['rng'] = rng
        opGrammar(kvs, {}, None)
    return None


def opDefine(kvs, parentkvs, xnode):
    global g_defines
    global g_touches
    name = assertObj(kvs['id'])
    if kvs.pop('TOUCH', False):
        g_touches.append(name)

    kvs['_env']['define'] = name
    kvs['_xnode'] = xnode
    kvs['_nodepath'] = []
    g_defines[name] = kvs
    return None


def opRef(kvs, parentkvs, _):
    global g_defines
    ref = kvs['id']
    assert ref in g_defines, "Can't find <define> '%s'." % ref
    if not kvs.get('_preserve') and isinstance(g_defines[ref], NodeList):
        nodes = g_defines[ref]
        if nodes.uniform() == 'Member':
            nodes = deepcopy(nodes)
        return nodes

    xnode = g_defines[ref].pop('_xnode')
    if kvs.get('_preserve'):
        kvs['_nodepath'].pop()
        kvs = deepupdate(g_defines[ref], kvs)
    else:
        deepupdate(kvs, g_defines[ref])

    # Preset it to avoid recursion
    save = g_defines[ref]
    g_defines[ref] = NodeList()
    nodes = _traverse(xnode.childNodes, kvs)
    if kvs.get('pack', False):
        # Pack all members into a pseudo Struct.
        assert nodes.uniform() is 'Member', kvs
        typeid = TypeTable().register('Struct', kvs, nodes)
        nodes = NodeList(createMember(typeid, kvs))

    # Rewrite it with NodeList to indicate *PARSED*.
    if kvs.get('_preserve'):
        g_defines[ref] = save
    else:
        g_defines[ref] = nodes

    if nodes.uniform() == 'Member':
        nodes = deepcopy(nodes)
    return nodes


def opElement(kvs, parentkvs, xnode):
    typeid = TypeTable().getByLocation('String')['id']
    nodes = _traverse(xnode.childNodes, kvs)
    if nodes:
        if nodes.uniform() == 'Member':
            typeid = TypeTable().register('Struct', kvs, nodes)
        else:
            assert nodes.uniform() == 'Builtin', nodes.uniform()
            typeid = assertOnlyOne(nodes)['id']
    return NodeList(createMember(typeid, kvs))


def opOptional(kvs, parentkvs, xnode):
    nodes = _traverse(xnode.childNodes, kvs)
    assert not nodes or nodes.uniform() == 'Member'
    for node in nodes:
        node['opt'] = True
    return nodes


def opAttribute(kvs, parentkvs, xnode):
    typeid = TypeTable().getByLocation('String')['id']
    nodes = _traverse(xnode.childNodes, kvs)
    if nodes:
        node = assertOnlyOne(nodes)
        if node['meta'] == 'Value':
            kvs['values'] = [node['value']]
            meta = kvs.get('meta')
            if not meta:
                meta = 'Constant'
            typeid = TypeTable().register(meta, kvs)
        else:
            assert nodes.uniform() in ['Builtin', 'Enum']
            typeid = node['id']

    return NodeList(createMember(typeid, kvs))


def opData(kvs, parentkvs, xnode):
    if 'meta' in kvs:
        meta = kvs['meta']
    else:
        meta = Terms.camelize(xnode.getAttribute('type'))

    typeid = TypeTable().register(meta, kvs)
    return NodeList(TypeTable().get(typeid))


def opParam(kvs, parentkvs, xnode):
    return None


def opChoice(kvs, parentkvs, xnode):
    nodes = _traverse(xnode.childNodes, kvs)
    if nodes.uniform() == 'Value':
        children = [child['value'] for child in nodes]
        typeid = TypeTable().register('Enum', kvs, children)
        return NodeList(TypeTable().get(typeid))

    if kvs.get('union'):
        # Pack all members into a pseudo Struct.
        assert nodes.uniform() is 'Member', kvs
        typeid = TypeTable().register('Struct', kvs, nodes)
        kvs['id'] = kvs['union']
        kvs['hint'] = 'union'
        nodes = NodeList(createMember(typeid, kvs))

    return nodes


def opValue(kvs, parentkvs, xnode):
    return NodeList({'meta': 'Value', 'value': _getText(xnode)})


def opInterleave(kvs, parentkvs, xnode):
    return _traverse(xnode.childNodes, kvs)


def opText(kvs, parentkvs, xnode):
    return None


def opEmpty(kvs, parentkvs, xnode):
    return None


def opZeroOrMore(kvs, parentkvs, xnode):
    nodes = _traverse(xnode.childNodes, kvs)
    for node in nodes:
        node['more'] = True
        node['opt'] = True
    return nodes


def opOneOrMore(kvs, parentkvs, xnode):
    nodes = _traverse(xnode.childNodes, kvs)
    for node in nodes:
        node['more'] = True
    return nodes


def opGroup(kvs, parentkvs, xnode):
    nodes = _traverse(xnode.childNodes, kvs)
    assert nodes.uniform() == 'Member'
    for node in nodes:
        node['opt'] = True
    return nodes


def opAnyName(kvs, parentkvs, xnode):
    _resetParentNames(parentkvs, '_Any_')
    return None


def opName(kvs, parentkvs, xnode):
    _resetParentNames(parentkvs, _getText(kvs['xnode']))
    return None


def mendParent(member, parent):
    mtype = TypeTable().get(member['_typeid'])
    assert mtype, member
    if mtype['meta'] == 'Struct':
        if mtype['_env']['define'] != parent['_env']['define']:
            parent = None

        mtype['_parent'] = parent
        nextp = parent if mtype['unpack'] else mtype
        for child in mtype['members']:
            mendParent(child, nextp)


class CodeWriter(object):
    def __init__(self, args):
        self._cmd = args.cmd
        self._files = {}
        self._filters = {}
        self._filters['structure'] = args.kinds and 's' in args.kinds
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
            f.write('/* Generated by rng2c/generator.py */\n\n')
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
            if not target or target == atype['id']:
                if extname == '.h':
                    info = Terms.upperInitial(kind)
                    if atype['unpack']:
                        parent = atype['_parent']['name']
                        info += ' (Unpack: expose to "%s".)' % parent
                    elif not atype[kind].get('output'):
                        info += ' (Disabled: NO OUTPUT for "%s".)' % kind
                    print('\n###### %s ######' % info)
                    print('\n[.h]')
                else:
                    print('\n[.c]')
                print('\n' + content)
            return

        assert self._cmd == 'generate'

        if atype[kind].get('output'):
            lfs = '\n' if extname == '.h' else '\n\n'
            path = atype['_env']['builddir'] + '/' + atype[kind]['output']
            f = self._getFile(path, extname)
            f.write(lfs + content + '\n')

    def complete(self):
        for name in self._files:
            self._files[name].close()
        self._files.clear()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=TOOL_DESC)
    subparsers = parser.add_subparsers(dest='cmd')
    parser_list = subparsers.add_parser('list', help='list all types')
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
        print('###### RNG2C: start ... ######')
        if not args.kinds:
            print("[dry run]: no kinds specified for 'generate'")

    timestamp = datetime.now()
    topdir = assertObj(os.environ.get('topdir', None))
    builddir = assertObj(os.environ.get('builddir', None))
    entries = assertObj(os.environ.get('entries', None))
    initDirectiveSchema(topdir + '/rng2c/schema.json')

    for entry in entries.split():
        env = {'_env': {'rng': entry, 'topdir': topdir, 'builddir': builddir}}
        opGrammar(env, {}, None)

    for atype in TypeTable().values():
        if atype['meta'] == 'Struct' and not atype['unpack']:
            for member in atype['members']:
                mendParent(member, atype)

    if args.cmd == 'list':
        print('%s  %-16s  %s' % ('SHORT_ID', 'META', 'LOCATION'))
        for atype in TypeTable().values():
            assert 'id' in atype, atype
            print('%-8s  %-16s  %s' % (atype['id'][:8], atype['meta'],
                                       atype['location']))
        sys.exit(0)
    elif args.cmd == 'show':
        assert args.target, args
        if '/' in args.target or isBuiltin(args.target):
            atype = TypeTable().getByLocation(args.target)
        elif len(args.target) == 40:
            atype = TypeTable().get(args.target)
        else:
            atype = TypeTable().getByPartialID(args.target)
        if not atype:
            sys.exit(0)

        args.target = atype['id']
        showDirective(atype)
        if isBuiltin(atype['meta']):
            print('\n###### Builtin details ######\n')
            if atype.get('name', None):
                ctype = atype['name']
            else:
                ctype = BUILTIN_TYPES.get(atype['meta'])['ctype']
            print("ctype: %s\n" % ctype)

    writer = CodeWriter(args)
    for atype in TypeTable().values():
        if atype['meta'] in ['Struct', 'Enum']:
            makeStructure(writer, atype)

    for atype in TypeTable().values():
        if atype['meta'] == 'Struct':
            makeClearFunc(writer, atype)

    for atype in TypeTable().values():
        if atype['meta'] == 'Struct':
            makeParseFunc(writer, atype)

    for atype in TypeTable().values():
        if atype['meta'] == 'Struct':
            makeFormatFunc(writer, atype)

    writer.complete()

    if args.cmd == 'generate':
        elapse = (datetime.now() - timestamp).microseconds
        print('\n###### RNG2C: elapse %d(us) ######\n' % elapse)

    sys.exit(0)
