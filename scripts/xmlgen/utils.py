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

from string import Template


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

    @classmethod
    def allcaps(cls, word):
        if len(word) == 0:
            return word
        parts = cls._split(word)
        ret = '_'.join([part.upper() for part in parts])
        for key, value in cls.caps.items():
            ret = ret.replace('_%s_' % key, '_%s_' % value)
        return ret


def render(template, **kwargs):
    return Template(template).substitute(kwargs)


class Block(list):
    def format(self, template, **args):
        if template:
            self.append(Template(template).substitute(**args))

    def extend(self, block):
        if isinstance(block, list):
            super(Block, self).extend(block)

    # ${_each_line_} is the only legal key for template
    # and represents each line of the block.
    def mapfmt(self, template, block):
        if not block or not template:
            return

        assert isinstance(block, list), block
        for line in block:
            if line:
                self.append(Template(template).substitute(_each_line_=line))
            else:
                self.append('')

    def newline(self, condition=True):
        if condition:
            super(Block, self).append('')

    def output(self, connector='\n'):
        return connector.join(self)


def dedup(alist):
    assert isinstance(alist, list)
    ret = []
    for e in alist:
        if e not in ret:
            ret.append(e)

    return ret
