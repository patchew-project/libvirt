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
from utils import singleton
from utils import dedup, counterName
from utils import BlockAssembler
from utils import Terms, singleline, indent, render, renderByDict

BUILTIN_TYPES = {
    'Bool': {},
    'String': {},
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


T_NAMESPACE_PARSE = '''
if (xmlopt)
    def->ns = xmlopt->ns;
if (def->ns.parse) {
    if (virXMLNamespaceRegister(ctxt, &def->ns) < 0)
        goto error;
    if ((def->ns.parse)(ctxt, &def->namespaceData) < 0)
        goto error;
}
'''

T_NAMESPACE_FORMAT_BEGIN = '''
if (def->namespaceData && def->ns.format)
    virXMLNamespaceFormatNS(buf, &def->ns);
'''

T_NAMESPACE_FORMAT_END = '''
if (def->namespaceData && def->ns.format) {
    if ((def->ns.format)(buf, def->namespaceData) < 0)
        return -1;
}
'''

T_CLEAR_FUNC_IMPL = '''
void
${typename}Clear(${typename}Ptr def)
{
    if (!def)
        return;

    ${body}
}
'''

T_CLEAR_FUNC_DECL = '''
void
${typename}Clear(${typename}Ptr def);
'''


def clearMember(member):
    mtype = TypeTable().get(member['type'])

    refname = 'def->%s' % member['name']
    if member.get('array'):
        refname += '[i]'

    code = ''
    if mtype['meta'] == 'Struct':
        if member['pointer'] and not mtype.get('external'):
            code = '%sClear(%s);' % (mtype['name'], refname)
            code += '\nVIR_FREE(%s);' % refname
        else:
            code = '%sClear(&%s);' % (mtype['name'], refname)
    elif mtype['name'] == 'String':
        code = 'VIR_FREE(%s);' % refname
    elif mtype['name'] in ['Chars', 'UChars']:
        code = 'memset(%s, 0, sizeof(%s));' % (refname, refname)
    elif not member.get('array'):
        code = '%s = 0;' % refname

    if member.get('specified'):
        assert not member.get('array'), "'specified' can't come with 'array'."
        code += '\n%s_specified = false;' % refname

    if member.get('array') and code:
        counter = counterName(member['name'])
        if singleline(code):
            code = render(T_LOOP_SINGLE, counter=counter, body=code)
        else:
            code = render(T_LOOP_MULTI,
                          counter=counter, body=indent(code, 2))
        code += '\nVIR_FREE(def->%s);' % member['name']
        code += '\ndef->%s = 0;' % counter

    return code


T_CLEAR_NAMESPACE = '''
if (def->namespaceData && def->ns.free)
    (def->ns.free)(def->namespaceData);
'''


def makeClearFunc(writer, atype):
    if 'genparse' not in atype:
        return

    blocks = BlockAssembler()
    for member in atype['members']:
        blocks.append(clearMember(member))

    if 'namespace' in atype:
        blocks.append(T_CLEAR_NAMESPACE.strip())

    body = indent(blocks.output('\n\n'), 1)

    impl = render(T_CLEAR_FUNC_IMPL, typename=atype['name'], body=body)
    writer.write(atype, 'clearfunc', '.c', impl)

    decl = render(T_CLEAR_FUNC_DECL, typename=atype['name'])
    writer.write(atype, 'clearfunc', '.h', decl)


#
# Templates for parsing member block
#
T_READ_ATTR_BY_PROP = '${mdvar} = virXMLPropString(node, "${tname}");'
T_READ_ELEM_BY_PROP = '${mdvar} = virXMLChildNode(node, "${tname}");'
T_READ_ELEM_CONTENT = '${mdvar} = virXMLChildNodeContent(node, "${tname}");'

T_PARSE_MEMBER_MORE = '''
${number} = virXMLChildNodeSet(node, "${tname}", &nodes);
if (${number} > 0) {
    size_t i;

    if (VIR_ALLOC_N(def->${name}, ${number}) < 0)
        goto error;

    for (i = 0; i < ${number}; i++) {
        xmlNodePtr tnode = nodes[i];
        ${item}
    }
    def->${counter} = ${number};
    VIR_FREE(nodes);
} else if (${number} < 0) {
    virReportError(VIR_ERR_XML_ERROR, "%s",
                   _("Invalid ${tname} element found."));
    goto error;
}${report_missing}
'''

T_CHECK_INVALID_ERROR = '''
if (${tmpl}) {
    virReportError(VIR_ERR_XML_ERROR,
                   _("Invalid '${tname}' setting '%s' in '%s'"),
                   ${mdvar}, instname);
    goto error;
}
'''

T_MISSING_ERROR = '''
{
    virReportError(VIR_ERR_XML_ERROR,
                   _("Missing '${tname}' setting in '%s'"),
                   instname);
    goto error;
}
'''

T_CHECK_MISSING_ERROR = 'if (${mdvar} == NULL) ' + T_MISSING_ERROR.strip()

T_ALLOC_MEMORY = '''
if (VIR_ALLOC(def->${name}) < 0)
    goto error;
'''

T_STRUCT_ASSIGNMENT_TEMPLATE = '''
if (${funcname}(${mdvar}, ${amp}${refname}, instname, NULL) < 0)
    goto error;
'''


def parseMember(member, atype, tmpvars):
    if not member.get('xmlattr') and not member.get('xmlelem'):
        return None

    tname = member['xmlattr'] if member.get('xmlattr') else member['xmlelem']
    mtype = TypeTable().get(member['type'])

    #
    # Helper functions
    #
    def _readXMLByXPath(mdvar):
        if member.get('xmlattr'):
            tmpl = T_READ_ATTR_BY_PROP
        elif mtype['meta'] == 'Struct':
            tmpl = T_READ_ELEM_BY_PROP
        else:
            tmpl = T_READ_ELEM_CONTENT
        return render(tmpl, mdvar=mdvar, tname=tname)

    def _assignValue(name, mdvar):
        refname = 'def->' + name
        if member.get('callback') or mtype['meta'] == 'Struct':
            if member.get('callback'):
                funcname = member['callback'] + 'ParseXML'
            else:
                funcname = mtype['name'] + 'ParseXML'

            tmpl = ''
            if member['pointer'] and not mtype.get('external'):
                tmpl += T_ALLOC_MEMORY
            tmpl += T_STRUCT_ASSIGNMENT_TEMPLATE

            if (member['pointer'] and not mtype.get('external')) or \
                    mtype['name'] in ['Chars', 'UChars']:
                amp = ''
            else:
                amp = '&'

            tmpl = render(tmpl, funcname=funcname, amp=amp, mdvar=mdvar)
        elif mtype['meta'] == 'Enum':
            formalname = mtype['name']
            if not formalname.endswith('Type'):
                formalname += 'Type'
            tmpl = '(def->${name} = %sFromString(%s)) <= 0' \
                % (formalname, mdvar)
        elif mtype['name'] == 'Bool':
            tmpl = 'virStrToBool(${mdvar}, "%s", &def->${name}) < 0' \
                % member.get('truevalue', 'yes')
        elif mtype['name'] == 'String':
            tmpl = 'def->${name} = g_strdup(${mdvar});'
        else:
            tmpl = None
            builtin = BUILTIN_TYPES.get(mtype['name'])
            if builtin:
                tmpl = builtin.get('conv', None)
                if tmpl:
                    tmpl += ' < 0'

        if not tmpl:
            return None

        if not member.get('callback') and mtype['meta'] != 'Struct' and \
                mdvar.endswith('Str') and (mtype['name'] != 'String'):
            tmpl = render(T_CHECK_INVALID_ERROR,
                          tmpl=tmpl, tname=tname, mdvar=mdvar)

        ret = render(tmpl, refname=refname, name=name,
                     tname=tname, mdvar=mdvar)

        if member.get('specified') and not member.get('array'):
            ret += '\ndef->%s_specified = true;' % name
        return ret

    def _assignValueOnCondition(name, mdvar):
        block = _assignValue(name, mdvar)
        if not block:
            return None

        if member.get('required'):
            ret = render(T_CHECK_MISSING_ERROR, mdvar=mdvar, tname=tname)
            ret += '\n\n' + block
            return ret

        if singleline(block):
            return render(T_IF_SINGLE, condition=mdvar, body=block)
        else:
            return render(T_IF_MULTI, condition=mdvar, body=indent(block, 1))

    #
    # Main routine
    #
    name = member['name']

    # For sequence-type member
    if member.get('array'):
        node_num = 'n%sNodes' % Terms.upperInitial(tname)
        tmpvars.append(node_num)
        tmpvars.append('nodes')
        counter = counterName(member['name'])

        report_missing = ''
        if member.get('required'):
            report_missing = ' else ' + render(T_MISSING_ERROR, tname=tname)

        if mtype['meta'] != 'Struct':
            item = 'def->%s[i] = virXMLNodeContentString(tnode);' % name
        else:
            item = _assignValue(name + '[i]', 'tnode')

        return render(T_PARSE_MEMBER_MORE, name=name, counter=counter,
                      number=node_num, item=indent(item, 2),
                      report_missing=report_missing, tname=tname)

    # For ordinary member
    mdvar = member['name'].replace('.', '_')
    if member.get('xmlattr') or mtype['meta'] != 'Struct':
        mdvar += 'Str'
    else:
        mdvar += 'Node'
    tmpvars.append(mdvar)

    blocks = BlockAssembler()
    blocks.append(_readXMLByXPath(mdvar))
    blocks.append(_assignValueOnCondition(name, mdvar))
    return blocks.output()


def align(funcname):
    return ' ' * (len(funcname) + 1)


T_PARSE_FUNC_DECL = '''
int
${funcname}(${args});
'''

T_PARSE_FUNC_IMPL = '''
int
${funcname}(${args})
{
    ${declare_vars}
    VIR_USED(instname);
    VIR_USED(opaque);

    if (!def)
        goto error;

    ${body}

    return 0;

 error:
    ${cleanup_vars}
    ${typename}Clear(def);
    return -1;
}
'''

T_PARSE_FUNC_POST_INVOKE = '''
if (${funcname}Hook(${args}) < 0)
    goto error;
'''


def _handleTmpVars(tmpvars):
    heads, tails = [], []
    tmpvars = dedup(tmpvars)
    for var in tmpvars:
        if var == 'nodes':
            heads.append('xmlNodePtr *nodes = NULL;')
            tails.append('VIR_FREE(nodes);')
        elif var.endswith('Str'):
            heads.append('g_autofree char *%s = NULL;' % var)
        elif var.endswith('Node'):
            heads.append('xmlNodePtr %s = NULL;' % var)
        else:
            assert var.endswith('Nodes') and var.startswith('n')
            heads.append('int %s = 0;' % var)

    return '\n'.join(heads), '\n'.join(tails)


def makeParseFunc(writer, atype):
    if 'genparse' not in atype:
        return

    typename = atype['name']
    funcname = typename + 'ParseXML'
    alignment = align(funcname)

    formal_args = [
        'xmlNodePtr node', typename + 'Ptr def',
        'const char *instname', 'void *opaque'
    ]

    actual_args = ['node', 'def', 'instname', 'opaque']

    if 'namespace' in atype:
        formal_args.append('xmlXPathContextPtr ctxt')
        actual_args.append('ctxt')
        formal_args.append('virNetworkXMLOptionPtr xmlopt')
        actual_args.append('xmlopt')

    kwargs = {'funcname': funcname, 'typename': typename,
              'args': (',\n%s' % alignment).join(formal_args)}

    tmpvars = []
    blocks = BlockAssembler()
    for member in atype['members']:
        blocks.append(parseMember(member, atype, tmpvars))

    decl = renderByDict(T_PARSE_FUNC_DECL, kwargs)

    if atype['genparse'] in ['withhook', 'concisehook']:
        if atype['genparse'] == 'withhook':
            for var in tmpvars:
                if var.endswith('Str') or var.endswith('Node') or \
                        var.endswith('Nodes') and var.startswith('n'):
                    actual_args.append(var)

        actual_args = ', '.join(actual_args) if actual_args else ''
        post = render(T_PARSE_FUNC_POST_INVOKE, funcname=funcname,
                      args=actual_args)
        blocks.append(post)

        if atype['genparse'] == 'withhook':
            for var in tmpvars:
                line = None
                if var.endswith('Str'):
                    line = 'const char *' + var
                elif var.endswith('Node'):
                    line = 'xmlNodePtr ' + var
                elif var.endswith('Nodes') and var.startswith('n'):
                    line = 'int ' + var

                if line:
                    formal_args.append(line)

        connector = ',\n' + alignment + 4 * ' '
        decl += '\n' + render(T_PARSE_FUNC_DECL, funcname=funcname + 'Hook',
                              args=connector.join(formal_args))

    writer.write(atype, 'parsefunc', '.h', decl)

    if 'namespace' in atype:
        blocks.append(T_NAMESPACE_PARSE.strip())

    kwargs['body'] = indent(blocks.output('\n\n'), 1)

    declare_vars, cleanup_vars = _handleTmpVars(tmpvars)
    kwargs['declare_vars'] = indent(declare_vars, 1)
    kwargs['cleanup_vars'] = indent(cleanup_vars, 1)

    impl = renderByDict(T_PARSE_FUNC_IMPL, kwargs)
    writer.write(atype, 'parsefunc', '.c', impl)


T_FORMAT_FUNC_DECL = '''
int
${typename}FormatBuf(virBufferPtr buf,
${alignment}const char *name,
${alignment}const ${typename} *def,
${alignment}void *opaque);
'''

T_FORMAT_FUNC_IMPL = '''
int
${typename}FormatBuf(virBufferPtr buf,
${alignment}const char *name,
${alignment}const ${typename} *def,
${alignment}void *opaque)
{
    VIR_USED(opaque);

    if (!def)
        return 0;

    ${format_members}

    return 0;
}
'''

T_FORMAT_CHECK_DECL = '''
bool
${typename}Check(const ${typename} *def, void *opaque);
'''

T_FORMAT_CHECK_IMPL = '''
bool
${typename}Check(const ${typename} *def, void *opaque)
{
    VIR_USED(opaque);

    if (!def)
        return false;

    return ${check};
}
'''

T_FORMAT_ELEMENTS = '''
virBufferAddLit(buf, ">\\n");

virBufferAdjustIndent(buf, 2);

${elements}

virBufferAdjustIndent(buf, -2);
virBufferAsprintf(buf, "</%s>\\n", name);
'''

T_FORMAT_SHORTHAND = '''
if (!(${checks})) {
    virBufferAddLit(buf, "/>\\n");
    return 0;
}
'''

T_IF_SINGLE = '''
if (${condition})
    ${body}
'''

T_IF_MULTI = '''
if (${condition}) {
    ${body}
}
'''

T_LOOP_SINGLE = '''
if (def->${counter} > 0) {
    size_t i;
    for (i = 0; i < def->${counter}; i++)
        ${body}
}
'''

T_LOOP_MULTI = '''
if (def->${counter} > 0) {
    size_t i;
    for (i = 0; i < def->${counter}; i++) {
        ${body}
    }
}
'''

T_FORMAT_MEMBER_OF_ENUM = '''
const char *str = ${fullname}ToString(${var});
if (!str) {
    virReportError(VIR_ERR_INTERNAL_ERROR,
                   _("Unknown ${tname} type %d"),
                   ${var});
    return -1;
}
virBufferAsprintf(buf, "${layout}", str);
'''


def formatMember(member, require, ret_checks):
    if not member.get('xmlattr') and not member.get('xmlelem'):
        return None

    mtype = TypeTable().get(member['type'])

    #
    # Helper functions.
    #
    def _checkOnCondition(var):
        if member.get('array'):
            return None

        t = TypeTable().get(member['type'])

        ret = None
        if 'checkformat' in member:
            ret = '%s(&%s, opaque)' % (member['checkformat'], var)
        elif member['pointer']:
            ret = var
        elif member.get('specified'):
            ret = var + '_specified'
            if ret.startswith('&'):
                ret = ret[1:]
        elif t['meta'] == 'Struct':
            ret = '%sCheck(&%s, opaque)' % (t['name'], var)
        elif member.get('required'):
            pass
        elif t['meta'] == 'Enum':
            ret = var
        elif t['meta'] == 'Builtin':
            if t['name'] in ['Chars', 'UChars']:
                ret = var + '[0]'
            else:
                ret = var

        return ret

    def _handleMore(code):
        code = indent(code, 2)
        counter = counterName(member['name'])
        ret_checks.append('def->' + counter)
        if singleline(code):
            return render(T_LOOP_SINGLE, counter=counter, body=code)
        else:
            return render(T_LOOP_MULTI, counter=counter, body=code)

    def _format(layout, var):
        tmpl = '${funcname}(buf, "${layout}", ${var})'

        funcname = 'virBufferAsprintf'
        has_return = False
        if member.get('callback') or mtype['meta'] == 'Struct':
            if member.get('callback'):
                funcname = member['callback'] + 'FormatBuf'
            else:
                funcname = mtype['name'] + 'FormatBuf'

            has_return = True
            if not member['pointer'] and \
                    mtype['name'] not in ['Chars', 'UChars']:
                var = '&' + var

            var = '%s, opaque' % var
        elif mtype['meta'] == 'Enum':
            name = mtype['name']
            if not name.endswith('Type'):
                name += 'Type'
            tmpl = render(T_FORMAT_MEMBER_OF_ENUM,
                          fullname=name, tname=member['xmlattr'])
        elif mtype['meta'] in ['String', 'Chars', 'UChars']:
            funcname = 'virBufferEscapeString'
        elif mtype['name'] == 'Bool':
            truevalue = member.get('truevalue', 'yes')
            if truevalue == 'yes':
                var = '%s ? "yes" : "no"' % var
            elif truevalue == 'on':
                var = '%s ? "on" : "off"' % var
            else:
                var = '%s ? "%s" : ""' % (var, truevalue)

        code = render(tmpl, funcname=funcname, layout=layout, var=var)
        if has_return:
            code += ' < 0'
            code = render(T_IF_SINGLE, condition=code, body='return -1;')
        elif mtype['meta'] not in ['Enum']:
            code += ';'

        return code

    def _handleAttr(tagname, var):
        if 'xmlattr' not in member:
            return None

        fmt = '%s'
        if member.get('format.fmt'):
            fmt = member['format.fmt']
        elif mtype['meta'] == 'Builtin':
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
    else:
        tagname = member['xmlelem']

    if require == 'attribute':
        ret = _handleAttr(tagname, var)
    else:
        ret = _handleElem(tagname, var)

    if not ret:
        return None

    checks = _checkOnCondition(var)
    if checks:
        ret = indent(ret, 1)
        if singleline(ret):
            ret = render(T_IF_SINGLE, condition=checks, body=ret)
        else:
            ret = render(T_IF_MULTI, condition=checks, body=ret)

    if member.get('array'):
        return _handleMore(ret)

    if checks:
        if '&&' in checks or '||' in checks:
            checks = '(%s)' % checks
        ret_checks.append(checks)

    return ret


def makeFormatFunc(writer, atype):
    if 'genformat' not in atype:
        return

    #
    # Helper functions.
    #
    def _formatMembers():
        attrs = []
        elems = []
        check_attrs = []
        check_elems = []

        for member in atype['members']:
            attr = formatMember(member, 'attribute', check_attrs)
            if attr:
                attrs.append(attr)

            elem = formatMember(member, 'element', check_elems)
            if elem:
                elems.append(elem)

        ret = BlockAssembler()
        if len(check_attrs) == len(attrs) \
                and len(check_elems) == len(elems):
            checks = ' || '.join(check_attrs + check_elems)
            atype['check'] = checks
            ret.append(render(T_IF_SINGLE, condition='!(%s)' % checks,
                              body='return 0;'))

        ret.append('virBufferAsprintf(buf, "<%s", name);')

        if 'namespace' in atype:
            ret.append(T_NAMESPACE_FORMAT_BEGIN.strip())

        ret.extend(attrs)

        if elems:
            if attrs and len(check_elems) == len(elems):
                checks = ' || '.join(check_elems)
                ret.append(render(T_FORMAT_SHORTHAND, checks=checks))

            elements = '\n\n'.join(elems)
            if 'namespace' in atype:
                elements += '\n\n' + T_NAMESPACE_FORMAT_END.strip()

            ret.append(render(T_FORMAT_ELEMENTS, elements=elements))
        else:
            ret.append('virBufferAddLit(buf, "/>\\n");')

        return ret.output('\n\n')

    #
    # Main routine of formating.
    #
    typename = atype['name']
    alignment = align(typename + 'FormatBuf')

    kwargs = {'alignment': alignment, 'typename': typename,
              'format_members': indent(_formatMembers(), 1)}

    decl = renderByDict(T_FORMAT_FUNC_DECL, kwargs)
    writer.write(atype, 'formatfunc', '.h', decl)

    impl = renderByDict(T_FORMAT_FUNC_IMPL, kwargs)
    writer.write(atype, 'formatfunc', '.c', impl)

    if atype.get('check'):
        decl = render(T_FORMAT_CHECK_DECL, typename=typename)
        writer.write(atype, 'formatfunc', '.h', decl)

        impl = render(T_FORMAT_CHECK_IMPL,
                      typename=typename, check=atype['check'])
        writer.write(atype, 'formatfunc', '.c', impl)


def showDirective(atype):
    print('\n###### Directive ######\n')
    print(json.dumps(atype, indent=4))
