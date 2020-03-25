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

import re
import string
import hashlib


def singleton(cls):
    _instances = {}

    def inner():
        if cls not in _instances:
            _instances[cls] = cls()
        return _instances[cls]
    return inner


class Terms(object):
    abbrs = ['uuid', 'pci', 'zpci', 'ptr', 'mac', 'mtu', 'dns', 'ip', 'dhcp']
    plurals = {'address': 'addresses'}
    caps = {'NET_DEV': 'NETDEV', 'MACTABLE': 'MAC_TABLE'}

    @classmethod
    def _split(cls, word):
        ret = []
        if not word:
            return ret
        head = 0
        for pos in range(1, len(word)):
            if word[pos].isupper() and not word[pos - 1].isupper():
                ret.append(word[head:pos])
                head = pos
        ret.append(word[head:])
        return ret

    @classmethod
    def pluralize(cls, word):
        ret = cls.plurals.get(word, None)
        return ret if ret else word + 's'

    # Don't use str.capitalize() which force other letters to be lowercase.
    @classmethod
    def upperInitial(cls, word):
        if not word:
            return ''
        if word in cls.abbrs:
            return word.upper()
        if len(word) > 0 and word[0].isupper():
            return word
        return word[0].upper() + word[1:]

    @classmethod
    def camelize(cls, word):
        if not word:
            return ''
        parts = cls._split(word)
        parts = map(cls.upperInitial, parts)
        return ''.join(parts)

    @classmethod
    def allcaps(cls, word):
        if len(word) == 0:
            return word
        parts = cls._split(word)
        ret = '_'.join([part.upper() for part in parts])
        for key, value in cls.caps.items():
            ret = ret.replace('_%s_' % key, '_%s_' % value)
        return ret


def assertOnlyOne(objs):
    assert len(objs) == 1 and objs[0], len(objs)
    return objs[0]


def assertObj(obj, msg=''):
    assert obj, msg
    return obj


def singleline(code):
    return len(re.findall(r'\n', code.strip())) == 0


def indent(block, count, unit=4):
    if not block:
        return ''
    lines = []
    for line in block.strip().split('\n'):
        lines.append(' ' * unit * count + line if line else '')
    return '\n'.join(lines).strip()


def render(template, **kwargs):
    return string.Template(template).safe_substitute(kwargs).strip()


def renderByDict(template, dictionary):
    return string.Template(template).safe_substitute(**dictionary).strip()


def deepupdate(target, source):
    assert isinstance(target, dict)
    assert isinstance(source, dict)
    for key, value in source.items():
        if key not in target:
            target[key] = value
            continue

        if isinstance(value, dict):
            deepupdate(target[key], value)
        else:
            target[key] = value

    return target


class BlockAssembler(list):
    def append(self, block):
        if block:
            super(BlockAssembler, self).append(block)

    def output(self, connector='\n'):
        return connector.join(self)


def sha1ID(unitext):
    sha1 = hashlib.sha1()
    sha1.update(unitext.encode())
    return sha1.hexdigest()


def dedup(alist):
    assert isinstance(alist, list)
    ret = []
    for e in alist:
        if e not in ret:
            ret.append(e)

    return ret


def counterName(name):
    name = Terms.pluralize(name)
    if not name.islower():
        name = Terms.upperInitial(name)
    return 'n' + name
