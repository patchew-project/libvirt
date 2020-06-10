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


def singleton(cls):
    _instances = {}

    def inner():
        if cls not in _instances:
            _instances[cls] = cls()
        return _instances[cls]
    return inner


class Terms(object):
    abbrs = ['uuid', 'pci', 'zpci', 'ptr', 'mac', 'mtu', 'dns', 'ip', 'dhcp']
    plurals = {'addresses': 'address'}

    @classmethod
    def singularize(cls, name):
        ret = cls.plurals.get(name, None)
        if ret:
            return ret
        assert name.endswith('s')
        return name[:-1]

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


class BlockAssembler(list):
    def append(self, block):
        if block:
            super(BlockAssembler, self).append(block)

    def output(self, connector='\n'):
        return connector.join(self)


def dedup(alist):
    assert isinstance(alist, list)
    ret = []
    for e in alist:
        if e not in ret:
            ret.append(e)

    return ret


def counterName(name):
    if not name.islower():
        name = Terms.upperInitial(name)
    return 'n' + name
