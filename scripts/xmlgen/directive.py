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

import json
from collections import OrderedDict
from utils import singleton, dedup, Block, Terms, render

BUILTIN_TYPES = {
    'String': {},
    'Bool': {},
    'Chars': {
        'conv': 'virStrcpyStatic(def->${name}, ${name}Str)'
    },
    'UChars': {
        'conv': 'virStrcpyStatic((char *)def->${name}, ${mdvar})'
    },
    'Int': {
        'fmt': '%d',
        'conv': 'virStrToLong_i(${mdvar}, NULL, 0, &def->${name})'
    },
    'UInt': {
        'fmt': '%u',
        'conv': 'virStrToLong_uip(${mdvar}, NULL, 0, &def->${name})'
    },
    'ULong': {
        'fmt': '%lu',
        'conv': 'virStrToLong_ulp(${mdvar}, NULL, 0, &def->${name})'
    },
    'ULongLong': {
        'fmt': '%llu',
        'conv': 'virStrToLong_ullp(${mdvar}, NULL, 0, &def->${name})'
    },
    'U8': {
        'fmt': '%u',
        'conv': 'virStrToLong_u8p(${mdvar}, NULL, 0, &def->${name})'
    },
    'U32': {
        'fmt': '%u',
        'conv': 'virStrToLong_uip(${mdvar}, NULL, 0, &def->${name})'
    },
    'Time': {
        'conv': 'virStrToTime(${mdvar}, &def->${name})'
    },
}


@singleton
class TypeTable(OrderedDict):
    def __init__(self):
        OrderedDict.__init__(self)
        for name, kvs in BUILTIN_TYPES.items():
            kvs['name'] = name
            kvs['meta'] = 'Builtin'
            self[name] = kvs

    def register(self, kvs):
        name = kvs['name']
        if name not in self:
            self[name] = kvs
        return name

    def get(self, name):
        if name in self:
            return self[name]
        return {'meta': 'Struct', 'name': name, 'external': True}

    def check(self, name):
        return name in self


T_NAMESPACE_PARSE = [
    'if (xmlopt)',
    '    def->ns = xmlopt->ns;',
    'if (def->ns.parse) {',
    '    if (virXMLNamespaceRegister(ctxt, &def->ns) < 0)',
    '        goto error;',
    '    if ((def->ns.parse)(ctxt, &def->namespaceData) < 0)',
    '        goto error;',
    '}'
]

T_NAMESPACE_FORMAT_BEGIN = [
    'if (def->namespaceData && def->ns.format)',
    '    virXMLNamespaceFormatNS(buf, &def->ns);'
]

T_NAMESPACE_FORMAT_END = [
    'if (def->namespaceData && def->ns.format) {',
    '    if ((def->ns.format)(buf, def->namespaceData) < 0)',
    '        return -1;',
    '}'
]


def funcSignature(rtype, name, args):
    alignment = ' ' * (len(name) + 1)
    connector = ',\n' + alignment

    ret = []
    ret.append(rtype)
    ret.append('%s(%s)' % (name, connector.join(args)))
    return Block(ret)


def counterName(member):
    if isinstance(member['array'], bool):
        return 'n' + member['name']
    return member['array']


def loop(count, block):
    assert isinstance(block, list) and len(block) > 0
    lp = ' {' if len(block) > 1 else ''

    ret = Block()
    ret.format('if (${count} > 0) {', count=count)
    ret.format('    size_t i;')
    ret.format('    for (i = 0; i < ${count}; i++)${lp}', count=count, lp=lp)
    ret.mapfmt('        ${_each_line_}', block)
    ret.format('    }' if lp else None)
    ret.format('}')
    return ret


def ispointer(member):
    mtype = TypeTable().get(member['type'])
    return member['pointer'] and not mtype.get('external')


def clearMember(member, pre_name=None):

    # Callback for make_switch
    def _switch_clear_cb(child, switch, _):
        return clearMember(child, switch['name'])

    if member.get('xmlswitch'):
        return makeSwitch(member, _switch_clear_cb, None)

    mtype = TypeTable().get(member['type'])

    if pre_name:
        ref = 'def->%s.%s' % (pre_name, member['name'])
    else:
        ref = 'def->%s' % member['name']

    if member.get('array'):
        ref += '[i]'

    ret = Block()
    if mtype['meta'] == 'Struct':
        if ispointer(member):
            ret.format('${tname}Clear(${ref});', tname=mtype['name'], ref=ref)
            ret.format('g_free(${ref});', ref=ref)
            ret.format('${ref} = NULL;', ref=ref)
        else:
            ret.format('${tname}Clear(&${ref});', tname=mtype['name'], ref=ref)
    elif mtype['name'] == 'String':
        ret.format('g_free(${ref});', ref=ref)
        ret.format('${ref} = NULL;', ref=ref)
    elif mtype['name'] in ['Chars', 'UChars']:
        ret.format('memset(${ref}, 0, sizeof(${ref}));', ref=ref)
    elif not member.get('array'):
        ret.format('${ref} = 0;', ref=ref)

    if member.get('specified'):
        assert not member.get('array'), "'specified' can't come with 'array'."
        if isinstance(member['specified'], str):
            ret.format('def->${sname} = false;', sname=member['specified'])
        else:
            ret.format('${ref}_specified = false;', ref=ref)

    if member.get('array') and len(ret) > 0:
        count = 'def->' + counterName(member)
        ret = loop(count, ret)
        ret.format('g_free(def->${name});', name=member['name'])
        ret.format('def->${name} = 0;', name=member['name'])
        ret.format('${count} = 0;', count=count)

    return ret


def makeClearFunc(writer, atype):
    if 'genparse' not in atype:
        return

    args = ['%sPtr def' % atype['name']]
    signature = funcSignature('void', atype['name'] + 'Clear', args)
    writer.write(atype, 'clearfunc', '.h', signature.output() + ';')

    ret = Block()
    ret.extend(signature)
    ret.format('{')
    ret.format('    if (!def)')
    ret.format('        return;')
    ret.newline()

    delay_members = []
    for member in atype['members']:
        if member.get('delay_clear'):
            delay_members.append(member)
            continue

        ret.mapfmt('    ${_each_line_}', clearMember(member))
        ret.newline()

    for member in delay_members:
        ret.mapfmt('    ${_each_line_}', clearMember(member))
        ret.newline()

    if 'namespace' in atype:
        ret.format('    if (def->namespaceData && def->ns.free)')
        ret.format('        (def->ns.free)(def->namespaceData);')
    else:
        ret.pop()   # Remove last newline

    ret.format('}')
    writer.write(atype, 'clearfunc', '.c', ret.output())


def reportInvalid(tname, mdvar):
    ret = Block()
    ret.append('virReportError(VIR_ERR_XML_ERROR,')
    ret.append("               _(\"Invalid '%s' setting '%s' in '%s'\"),")
    ret.format('               "${tname}", ${mdvar}, instname);',
               tname=tname, mdvar=mdvar)
    return ret


def reportMissing(tname):
    ret = Block()
    ret.append('virReportError(VIR_ERR_XML_ERROR,')
    ret.append("               _(\"Missing '%s' setting in '%s'\"),")
    ret.format('               "${tname}", instname);', tname=tname)
    return ret


def checkError(kind, cond, tname, mdvar):
    if kind == 'Invalid':
        report = reportInvalid(tname, mdvar)
    elif kind == 'Missing':
        report = reportMissing(tname)
    else:
        assert False, '%s is unsupported.' % kind

    ret = Block()
    ret.format('if (${cond}) {', cond=cond)
    ret.mapfmt('    ${_each_line_}', report)
    ret.append('    goto error;')
    ret.append('}')
    return ret


def parseMember(member, parent, tmpvars, pre_name=None):
    mtype = TypeTable().get(member['type'])

    #
    # Helper functions
    #
    def _middle_var(member):
        ret = member['name'].replace('.', '_')
        if member.get('xmlgroup'):
            ret = 'node'
        elif mtype['name'] == 'String':
            ret = 'def->' + ret
        elif member.get('xmlattr') or mtype['meta'] != 'Struct':
            ret += 'Str'
        else:
            ret += 'Node'
        return ret

    def _read_xml_func(mdvar, tname):
        if member.get('xmlattr'):
            if '/' in tname:
                funcname = 'virXMLChildPropString'
            else:
                funcname = 'virXMLPropString'
        elif member.get('xmlelem'):
            if mtype['meta'] == 'Struct':
                funcname = 'virXMLChildNode'
            else:
                funcname = 'virXMLChildNodeContent'
        else:
            return None

        return render('${mdvar} = ${funcname}(node, "${tname}");',
                      mdvar=mdvar, funcname=funcname, tname=tname)

    def _assign_struct(name, member, mdvar):
        ret = Block()
        if ispointer(member):
            ret.format('def->${name} = g_new0(typeof(*def->${name}), 1);',
                       name=name)

        func = mtype['name'] + 'ParseXML'
        amp = '' if ispointer(member) else '&'
        tmpl = '${func}(${mdvar}, ${amp}def->${name}, instname, def, opaque)'
        fn = render(tmpl, func=func, mdvar=mdvar, amp=amp, name=name)
        return if_cond(fn + ' < 0', ['goto error;'])

    def _assign_non_struct(name, member, mdvar):
        if mtype['meta'] == 'Enum':
            typename = mtype['name']
            if not typename.endswith('Type'):
                typename += 'Type'
            expr = render('(def->${name} = ${typename}FromString(${mdvar}))',
                          name=name, typename=typename, mdvar=mdvar)
            expr += ' <= 0'
        elif mtype['name'] == 'Bool':
            truevalue = member.get('truevalue', 'yes')
            expr = render('virStrToBool(${mdvar}, "${truth}", &def->${name})',
                          mdvar=mdvar, truth=truevalue, name=name)
            expr += ' < 0'
        else:
            builtin = BUILTIN_TYPES.get(mtype['name'])
            assert builtin, mtype['name']
            tmpl = builtin.get('conv', None)
            if tmpl:
                expr = render(tmpl, name=name, mdvar=mdvar, tname=tname)
                expr += ' < 0'
            else:
                return None

        return checkError('Invalid', expr, tname, mdvar)

    def _parse_array(name, member, tname):
        num = 'n%sNodes' % Terms.upperInitial(tname)
        tmpvars.append(num)
        tmpvars.append('nodes')
        count = counterName(member)

        if mtype['meta'] == 'Struct':
            item = _assign_struct(name + '[i]', member, 'tnode')
        else:
            item = ['def->%s[i] = virXMLNodeContentString(tnode);' % name]

        ret = Block()
        ret.format('${num} = virXMLChildNodeSet(node, "${tname}", &nodes);',
                   num=num, tname=tname)
        ret.format('if (${num} > 0) {', num=num)
        ret.format('    size_t i;')
        ret.newline()
        ret.format('    def->${name} = g_new0(typeof(*def->${name}), ${num});',
                   name=name, num=num)
        ret.format('    for (i = 0; i < ${num}; i++) {', num=num)
        ret.format('        xmlNodePtr tnode = nodes[i];')
        ret.mapfmt('        ${_each_line_}', item)
        ret.format('    }')
        ret.format('    def->${count} = ${num};', count=count, num=num)
        ret.format('    g_free(nodes);')
        ret.format('    nodes = NULL;')
        ret.format('} else if (${num} < 0) {', num=num)
        ret.format('    virReportError(VIR_ERR_XML_ERROR,')
        ret.format('                   _("Invalid %s element found."),')
        ret.format('                   "${tname}");', tname=tname)
        ret.format('    goto error;')

        if member.get('required'):
            ret.append('} else {')
            ret.mapfmt('    ${_each_line_}', reportMissing(tname))
            ret.append('    goto error;')

        ret.append('}')
        return ret

    #
    # Main routine
    #
    if not member.get('xmlattr') and not member.get('xmlelem') \
            and not member.get('xmlgroup'):
        return None

    if pre_name:
        name = pre_name + '.' + member['name']
    else:
        name = member['name']

    tname = None
    if member.get('xmlattr'):
        tname = member['xmlattr']
    elif member.get('xmlelem'):
        tname = member['xmlelem']

    # For array member
    if member.get('array'):
        return _parse_array(name, member, tname)

    # For common member
    mdvar = _middle_var(member)
    if mdvar.endswith('Str') or mdvar.endswith('Node'):
        tmpvars.append(mdvar)

    block = Block()
    if tname:
        block.append(_read_xml_func(mdvar, tname))
        if member.get('required'):
            cond = render('${mdvar} == NULL', mdvar=mdvar)
            block.extend(checkError('Missing', cond, tname, mdvar))

    if mtype['meta'] == 'Struct':
        assignment = _assign_struct(name, member, mdvar)
    else:
        assignment = _assign_non_struct(name, member, mdvar)
    if not assignment:
        return block

    if member.get('specified'):
        if isinstance(member['specified'], str):
            assignment.format('def->${name} = true;', name=member['specified'])
        else:
            assignment.format('def->${name}_specified = true;', name=name)

    if tname:
        block.extend(if_cond(mdvar, assignment))
    else:
        block.extend(assignment)

    return block


def makeParseFunc(writer, atype):

    #
    # Helper functions
    #
    def _switch_parse_cb(child, switch, tmpvars):
        return parseMember(child, atype, tmpvars, switch['name'])

    def _members_block(tmpvars):
        block = Block()
        for member in atype['members']:
            if member.get('xmlswitch'):
                block.extend(makeSwitch(member, _switch_parse_cb, tmpvars))
            else:
                block.extend(parseMember(member, atype, tmpvars))
            block.newline()
        return block

    def _post_hook(tmpvars):
        if atype['genparse'] not in ['withhook', 'concisehook']:
            return None

        args = ['node', 'def', 'instname', 'parent', 'opaque']
        if 'namespace' in atype:
            args.append('ctxt')
            args.append('xmlopt')

        if atype['genparse'] == 'withhook':
            args.extend(tmpvars)
            if 'nodes' in args:
                args.remove('nodes')

        funcname = atype['name'] + 'ParseXMLHook'
        cond = '%s(%s) < 0' % (funcname, ', '.join(args))
        return if_cond(cond, ['goto error;'])

    def _handle_tmpvars(tmpvars):
        args, heads, tails = [], [], []
        for var in tmpvars:
            if var == 'nodes':
                heads.append('xmlNodePtr *nodes = NULL;')
                tails.append('g_free(nodes);')
                tails.append('nodes = NULL;')
            elif var.endswith('Str'):
                heads.append('g_autofree char *%s = NULL;' % var)
                args.append('const char *%s' % var)
            elif var.endswith('Node'):
                heads.append('xmlNodePtr %s = NULL;' % var)
                args.append('xmlNodePtr %s' % var)
            else:
                assert var.endswith('Nodes') and var.startswith('n')
                heads.append('int %s = 0;' % var)
                args.append('int %s' % var)

        if atype['genparse'] != 'withhook':
            args = []

        if heads:
            heads.append('')

        heads.append('if (!def)')
        heads.append('    goto error;')
        tails.append('%sClear(def);' % atype['name'])
        return args, heads, tails

    #
    # Composite
    #
    if 'genparse' not in atype:
        return

    typename = atype['name']
    funcname = typename + 'ParseXML'

    # Declare virXXXParseXML
    args = Block([
        'xmlNodePtr node',
        '%sPtr def' % typename,
        'const char *instname',
        'void *parent',
        'void *opaque'
    ])

    if 'namespace' in atype:
        args.append('xmlXPathContextPtr ctxt')
        args.append('virNetworkXMLOptionPtr xmlopt')

    signature = funcSignature('int', funcname, args)
    writer.write(atype, 'parsefunc', '.h', signature.output() + ';')

    # Prepare for implementation
    tmpvars = []
    parseblock = _members_block(tmpvars)
    tmpvars = dedup(tmpvars)
    tmpargs, headlines, cleanup = _handle_tmpvars(tmpvars)
    posthook = _post_hook(tmpvars)

    if atype['genparse'] in ['withhook', 'concisehook']:
        # Declare virXXXParseXMLHook
        signature = funcSignature('int', funcname + 'Hook', args + tmpargs)
        writer.write(atype, 'parsefunc', '.h', signature.output() + ';')
    else:
        # Without hook, instname, parent and opaque are unused.
        args[2] += ' G_GNUC_UNUSED'
        args[3] += ' G_GNUC_UNUSED'
        args[4] += ' G_GNUC_UNUSED'

    # Implement virXXXParseXML
    impl = funcSignature('int', funcname, args)
    impl.format('{')
    impl.mapfmt('    ${_each_line_}', headlines)
    impl.newline()
    impl.mapfmt('    ${_each_line_}', parseblock)

    if posthook:
        impl.mapfmt('    ${_each_line_}', posthook)
        impl.newline()

    if 'namespace' in atype:
        impl.extend('    ${_each_line_}', T_NAMESPACE_PARSE)

    impl.format('    return 0;')
    impl.newline()
    impl.format(' error:')
    impl.mapfmt('    ${_each_line_}', cleanup)
    impl.format('    return -1;')
    impl.format('}')

    writer.write(atype, 'parsefunc', '.c', impl.output())


def if_cond(condition, block):
    assert isinstance(block, list) and len(block) > 0
    lp = ' {' if len(block) > 1 else ''

    ret = Block()
    ret.format('if (${condition})${lp}', condition=condition, lp=lp)
    ret.mapfmt('    ${_each_line_}', block)
    ret.format('}' if lp else None)
    return ret


def formatMember(member, parent):
    mtype = TypeTable().get(member['type'])

    #
    # Helper functions.
    #
    def _checkOnCondition(var):
        ret = None
        if ispointer(member):
            ret = var
        elif member.get('specified'):
            if isinstance(member['specified'], str):
                ret = 'def->' + member['specified']
            else:
                ret = var + '_specified'
            if ret.startswith('&'):
                ret = ret[1:]
        elif mtype['meta'] == 'Struct':
            ret = '%sCheck(&%s, def, opaque)' % (mtype['name'], var)
        elif member.get('required'):
            pass
        elif mtype['meta'] == 'Enum':
            ret = var
        elif mtype['meta'] == 'Builtin':
            if mtype['name'] in ['Chars', 'UChars']:
                ret = var + '[0]'
            else:
                ret = var

        if 'formatflag' in member:
            flag = member['formatflag']
            if flag == '_ALWAYS_':
                return None

            exclusive = False
            reverse = False
            if flag[0] == '%':
                flag = flag[1:]
                exclusive = True
            if flag[0] == '!':
                flag = flag[1:]
                reverse = True
            cond = '(virXMLFlag(opaque) & %s)' % flag
            if reverse:
                cond = '!' + cond

            if ret and not exclusive:
                ret = ret + ' && ' + cond
            else:
                ret = cond

        return ret

    def _handleMore(code):
        counter = 'def->' + counterName(member)
        return loop(counter, code), counter

    def _format(layout, var):
        if mtype['meta'] == 'Struct':
            if not ispointer(member):
                var = '&' + var

            fname = mtype['name'] + 'FormatBuf'
            cond = '%s(buf, "%s", %s, def, opaque) < 0' % (fname, layout, var)
            return if_cond(cond, ['return -1;'])
        elif mtype['meta'] == 'Enum':
            name = mtype['name']
            if not name.endswith('Type'):
                name += 'Type'

            ret = Block()
            ret.format('const char *str = ${name}ToString(${var});',
                       name=name, var=var)
            ret.format('if (!str) {')
            ret.format('    virReportError(VIR_ERR_INTERNAL_ERROR,')
            ret.format('                   _("Unknown %s type %d"),')
            ret.format('                   "${tname}", ${var});',
                       tname=member['xmlattr'], var=var)
            ret.format('    return -1;')
            ret.format('}')
            ret.format('virBufferAsprintf(buf, "${layout}", str);',
                       layout=layout)
            return ret
        elif mtype['name'] == 'Bool':
            truevalue = member.get('truevalue', 'yes')
            if truevalue == 'yes':
                var = '%s ? "yes" : "no"' % var
            elif truevalue == 'on':
                var = '%s ? "on" : "off"' % var
            else:
                var = '%s ? "%s" : ""' % (var, truevalue)
            return ['virBufferEscapeString(buf, "%s", %s);' % (layout, var)]
        elif mtype['name'] == 'String':
            return ['virBufferEscapeString(buf, "%s", %s);' % (layout, var)]
        elif mtype['name'] == 'Time':
            return ['virTimeFormatBuf(buf, "%s", %s);' % (layout, var)]
        else:
            return ['virBufferAsprintf(buf, "%s", %s);' % (layout, var)]

    def _handleAttr(tagname, var):
        if 'xmlattr' not in member:
            return None

        fmt = '%s'
        if mtype['meta'] == 'Builtin':
            fmt = BUILTIN_TYPES[mtype['name']].get('fmt', '%s')

        layout = " %s='%s'" % (tagname, fmt)
        return _format(layout, var)

    def _handleElem(tagname, var):
        if 'xmlattr' in member:
            return None

        if mtype['meta'] != 'Struct':
            layout = '<%s>%%s</%s>\\n' % (tagname, tagname)
        else:
            layout = tagname

        code = _format(layout, var)
        return code

    #
    # Main routine
    #
    name = member['name']
    if member.get('array'):
        name = name + '[i]'
    var = 'def->' + name

    ret = None
    if 'xmlattr' in member:
        tagname = member['xmlattr']
        ret = _handleAttr(tagname, var)
    else:
        tagname = member['xmlelem']
        ret = _handleElem(tagname, var)

    if not ret:
        return None, None

    if member.get('array'):
        return _handleMore(ret)

    checks = _checkOnCondition(var)
    if checks:
        ret = if_cond(checks, ret)

    if checks:
        if '&&' in checks or '||' in checks:
            checks = '(%s)' % checks

    return ret, checks


def makeSwitch(switch, callback, opaque=None):
    assert switch.get('xmlswitch', None) and switch.get('switch_type', None)

    captype = Terms.allcaps(switch['switch_type'])
    block = Block()
    block.format('switch (def->${etype}) {', etype=switch['xmlswitch'])
    block.newline()
    for child in switch['members']:
        value = captype + '_' + Terms.allcaps(child['name'])
        block.format('case ${value}:', value=value)
        block.mapfmt('    ${_each_line_}', callback(child, switch, opaque))
        block.format('    break;')
        block.newline()
    block.format('case ${captype}_NONE:', captype=captype)
    block.format('case ${captype}_LAST:', captype=captype)
    block.format('    break;')
    block.format('}')
    return block


def makeFormatFunc(writer, atype):
    if 'genformat' not in atype:
        return

    #
    # Helper functions and classes.
    #
    def _handleHeads(tmpvars, typename, kind=''):
        tmpvars = dedup(tmpvars)
        heads = Block()
        if tmpvars:
            heads.mapfmt(
                'g_auto(virBuffer) ${_each_line_} = VIR_BUFFER_INITIALIZER;',
                tmpvars
            )
            heads.newline()

        heads.format('if (!def || !buf)')
        heads.format('    return 0;')

        if tmpvars:
            funcname = typename + 'Format' + kind + 'Hook'

            heads.newline()
            args = Block(['def', 'parent', 'opaque'])
            args.mapfmt('&${_each_line_}', tmpvars)
            heads.format('if (%s(%s) < 0)' % (funcname, ', '.join(args)))
            heads.format('    return -1;')

            args = Block([
                'const %s *def' % typename,
                'const void *parent',
                'const void *opaque'
            ])
            args.mapfmt('virBufferPtr ${_each_line_}', tmpvars)
            signature = funcSignature('int', funcname, args)
            writer.write(atype, 'formatfunc', '.h', signature.output() + ';')

            args = [
                'const %s *def' % typename,
                'const void *parent',
                'void *opaque',
                'bool value'
            ]
            funcname = typename + 'Check' + kind + 'Hook'
            signature = funcSignature('bool', funcname, args)
            writer.write(atype, 'formatfunc', '.h', signature.output() + ';')

        return '\n'.join(heads)

    def _format_group(child, switch, kind):
        kind = kind if kind else ''
        prefix = (switch['name'] + '.') if switch else ''
        mtype = TypeTable().get(child['type'])
        funcname = '%sFormat%s' % (mtype['name'], kind)
        var = 'def->%s%s' % (prefix, child['name'])
        if not ispointer(child):
            var = '&' + var
        r_check = '%sCheck%s(%s, def, opaque)' % (mtype['name'], kind, var)
        cond = '%s(buf, %s, def, opaque) < 0' % (funcname, var)
        return if_cond(cond, ['return -1;']), r_check

    def _switch_format_cb(child, switch, kind):
        return _format_group(child, switch, kind)[0]

    def _handle_wrap_attr(member):
        wrap, member['xmlattr'] = member['xmlattr'].split('/')
        ret, check = formatMember(member, atype)
        return ret, check, wrap

    def _switch_check_cb(child, switch, kind):
        return ['ret = %s;' % _format_group(child, switch, kind)[1]]

    def _prepare_member(member, atype):
        wrap, attr, attr_chk, elem, elem_chk = None, None, None, None, None
        if member.get('xmlswitch'):
            attr = makeSwitch(member, _switch_format_cb, 'Attr')
            elem = makeSwitch(member, _switch_format_cb, 'Elem')
            basename = atype['name'] + Terms.upperInitial(member['name'])
            attr_chk = '%sCheckAttr(def, opaque)' % basename
            elem_chk = '%sCheckElem(def, opaque)' % basename

            # Declare virXXX<UnionName>Check[Attr|Elem] for switch.
            for kind in ['Attr', 'Elem']:
                args = ['const %s *def' % atype['name'], 'void *opaque']
                decl = funcSignature('bool', basename + 'Check' + kind, args)
                writer.write(atype, 'formatfunc', '.h', decl.output() + ';')

                # Implement virXXX<UnionName>Check[Attr|Elem] for switch.
                checks = makeSwitch(member, _switch_check_cb, kind)

                args[1] += ' G_GNUC_UNUSED'
                impl = funcSignature('bool', basename + 'Check' + kind, args)
                impl.format('{')
                impl.format('    bool ret = false;')
                impl.format('    if (!def)')
                impl.format('        return false;')
                impl.newline()
                impl.mapfmt('    ${_each_line_}', checks)
                impl.newline()
                impl.format('    return ret;')
                impl.format('}')
                writer.write(atype, 'formatfunc', '.c', impl.output())

        elif member.get('xmlattr'):
            if '/' in member['xmlattr']:
                attr, attr_chk, wrap = _handle_wrap_attr(member)
            else:
                attr, attr_chk = formatMember(member, atype)
        elif member.get('xmlelem'):
            elem, elem_chk = formatMember(member, atype)
        elif member.get('xmlgroup'):
            attr, attr_chk = _format_group(member, None, 'Attr')
            elem, elem_chk = _format_group(member, None, 'Elem')
        return wrap, attr, attr_chk, elem, elem_chk

    def _prepare_hook(member):
        assert member.get('xmlattr') or member.get('xmlelem')
        buf = member['name'] + 'Buf'
        ret = if_cond('virBufferUse(&%s)' % buf,
                      ['virBufferAddBuffer(buf, &%s);' % buf])
        return ret, buf

    class _WrapItem:
        def __init__(self):
            self.attrs = Block()
            self.checks = []
            self.optional = True
            self.pos = 0

    def _prepare():
        attrs = Block()
        elems = Block()
        attr_checks = []
        elem_checks = []
        attr_hook_vars = []
        elem_hook_vars = []
        attrs_optional = True
        elems_optional = True
        wraps = OrderedDict()
        for member in atype['members']:
            if member.get('formathook'):
                block, hookvar = _prepare_hook(member)
                if member.get('xmlattr'):
                    attrs.extend(block)
                    attrs.newline(block)
                    attr_hook_vars.append(hookvar)
                elif member.get('xmlelem'):
                    elems.extend(block)
                    elems.newline(block)
                    elem_hook_vars.append(hookvar)
                else:
                    assert False, 'formathook is only with [xmlattr|xmlelem].'
            else:
                wrap, attr, attr_chk, elem, elem_chk = \
                    _prepare_member(member, atype)
                if wrap:
                    item = wraps.setdefault(wrap, _WrapItem())
                    item.pos = len(elems)
                    item.attrs.extend(attr)
                    item.attrs.newline()
                    item.checks.append(attr_chk)
                    if member.get('required'):
                        item.optional = False
                    continue

                attrs.extend(attr)
                attrs.newline(attr)
                elems.extend(elem)
                elems.newline(elem)
                if attr_chk:
                    attr_checks.append(attr_chk)
                if elem_chk:
                    elem_checks.append(elem_chk)
                if member.get('required'):
                    attrs_optional = False

        while wraps:
            wrap, item = wraps.popitem()
            lines = Block()
            lines.format('virBufferAddLit(buf, "<${name}");', name=wrap)
            lines.newline()
            lines.extend(item.attrs)
            lines.format('virBufferAddLit(buf, "/>\\n");')
            if item.optional:
                elem_checks.extend(item.checks)
                lines = if_cond(' || '.join(item.checks), lines)
                lines.newline()
            else:
                elems_optional = False

            for line in reversed(lines):
                elems.insert(item.pos, line)

        attr_checks = dedup(attr_checks)
        elem_checks = dedup(elem_checks)
        return (attrs, attr_checks, attrs_optional, attr_hook_vars,
                elems, elem_checks, elems_optional, elem_hook_vars)

    def _check_null(optional, checks, has_hook, kind=''):
        if not optional:
            return None

        # Declare virXXXCheck[Attr|Elem]
        typename = atype['name']
        funcname = typename + 'Check' + kind
        args = [
            'const %s *def' % typename,
            'const void *parent',
            'void *opaque'
        ]
        signature = funcSignature('bool', funcname, args)
        writer.write(atype, 'formatfunc', '.h', signature.output() + ';')

        # Implement virXXXFormat[Attr|Elem]
        check = ' || '.join(checks) if optional else 'true'
        if not check:
            check = 'true'

        if has_hook:
            check = '%sCheck%sHook(def, parent, opaque, %s)' \
                    % (typename, kind, check)

        args[1] += ' G_GNUC_UNUSED'
        args[2] += ' G_GNUC_UNUSED'

        impl = funcSignature('bool', funcname, args)
        impl.format('{')
        impl.format('    if (!def)')
        impl.format('        return false;')
        impl.newline()
        impl.format('    return ${check};', check=check)
        impl.format('}')
        writer.write(atype, 'formatfunc', '.c', impl.output())

        return if_cond('!%s(def, parent, opaque)' % funcname, ['return 0;'])

    def _compose_full(attrs, attr_checks, attrs_optional, attr_hook_vars,
                      elems, elem_checks, elems_optional, elem_hook_vars):
        has_hook = (attr_hook_vars or elem_hook_vars)
        typename = atype['name']

        # Declare virXXXFormatBuf
        args = [
            'virBufferPtr buf',
            'const char *name',
            'const %s *def' % typename,
            'const void *parent',
            'void *opaque'
        ]
        signature = funcSignature('int', typename + 'FormatBuf', args)
        writer.write(atype, 'formatfunc', '.h', signature.output() + ';')

        # Implement virXXXFormatBuf
        headlines = _handleHeads(attr_hook_vars + elem_hook_vars, typename)
        checknull = _check_null(attrs_optional and elems_optional,
                                attr_checks + elem_checks, has_hook)

        args[3] += ' G_GNUC_UNUSED'
        args[4] += ' G_GNUC_UNUSED'

        impl = funcSignature('int', typename + 'FormatBuf', args)
        impl.format('{')
        impl.mapfmt('    ${_each_line_}', headlines.split('\n'))
        impl.newline()

        if checknull:
            impl.mapfmt('    ${_each_line_}', checknull)
            impl.newline()

        impl.format('    virBufferAsprintf(buf, "<%s", name);')
        impl.newline()

        if 'namespace' in atype:
            impl.mapfmt('    ${_each_line}', T_NAMESPACE_FORMAT_BEGIN)

        impl.mapfmt('    ${_each_line_}', attrs)

        if elems:
            if attrs and elems_optional:
                impl.format('    if (!(${checks})) {',
                            checks=' || '.join(elem_checks))
                impl.format('        virBufferAddLit(buf, "/>\\n");')
                impl.format('        return 0;')
                impl.format('    }')
                impl.newline()

            if 'namespace' in atype:
                impl.mapfmt('    ${_each_line}', T_NAMESPACE_FORMAT_END)

            impl.format('    virBufferAddLit(buf, ">\\n");')
            impl.newline()
            impl.format('    virBufferAdjustIndent(buf, 2);')
            impl.newline()
            impl.mapfmt('    ${_each_line_}', elems)
            impl.format('    virBufferAdjustIndent(buf, -2);')
            impl.format('    virBufferAsprintf(buf, "</%s>\\n", name);')
        else:
            impl.format('    virBufferAddLit(buf, "/>\\n");')

        impl.newline()
        impl.format('    return 0;')
        impl.format('}')
        writer.write(atype, 'formatfunc', '.c', impl.output())

    def _compose_part(kind, block, checks, optional, hook_vars):
        typename = atype['name']
        funcname = typename + 'Format' + kind
        headlines = _handleHeads(hook_vars, atype['name'], kind)
        if not block:
            block = ['/* empty */', '']

        checknull = _check_null(optional, checks, len(hook_vars), kind)

        # Declare virXXXFormat[Attr|Elem]
        args = [
            'virBufferPtr buf',
            'const %s *def' % typename,
            'const void *parent',
            'void *opaque'
        ]
        signature = funcSignature('int', funcname, args)
        writer.write(atype, 'formatfunc', '.h', signature.output() + ';')

        # Implement virXXXFormat[Attr|Elem]
        args[2] += ' G_GNUC_UNUSED'
        args[3] += ' G_GNUC_UNUSED'

        impl = funcSignature('int', funcname, args)
        impl.format('{')
        impl.mapfmt('    ${_each_line_}', headlines.split('\n'))
        impl.newline()

        if checknull:
            impl.mapfmt('    ${_each_line_}', checknull)
            impl.newline()

        impl.mapfmt('    ${_each_line_}', block)
        impl.format('    return 0;')
        impl.format('}')
        writer.write(atype, 'formatfunc', '.c', impl.output())

    #
    # Main routine of formating.
    #
    (attrs, attr_checks, attrs_optional, attr_hook_vars,
     elems, elem_checks, elems_optional, elem_hook_vars) = _prepare()

    if atype['genformat'] in ['separate', 'onlyattrs', 'onlyelems']:
        if atype['genformat'] in ['separate', 'onlyattrs']:
            _compose_part('Attr', attrs, attr_checks,
                          attrs_optional, attr_hook_vars)

        if atype['genformat'] in ['separate', 'onlyelems']:
            _compose_part('Elem', elems, elem_checks,
                          elems_optional, elem_hook_vars)
    else:
        _compose_full(attrs, attr_checks, attrs_optional, attr_hook_vars,
                      elems, elem_checks, elems_optional, elem_hook_vars)


def showDirective(atype):
    print('\n[Directive]\n')
    print(json.dumps(atype, indent=4))
