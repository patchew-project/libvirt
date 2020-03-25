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
#   https://www.w3.org/TR/xmlschema-2/#decimal
#   https://json-schema.org/understanding-json-schema
#

import sys
import json
from collections import OrderedDict
from utils import singleton, assertOnlyOne, deepupdate
from utils import dedup, counterName
from utils import BlockAssembler, sha1ID
from utils import Terms, singleline, indent, render, renderByDict

g_schema = None

JSON_TYPE_MAP = {
    'null': (['NoneType'], None),
    'string': (['str', 'unicode'], ''),
    'array': (['list'], []),
    'object': (['dict'], {}),
    'boolean': (['bool'], False),
    'integer': (['int'], 0),
}


def initDirectiveSchema(path):
    def _resolveRef(schema, definitions):
        if '$ref' in schema:
            link = schema.pop('$ref')
            link = link[len('#/definitions/'):]
            definition = _resolveRef(definitions[link], definitions)
            schema.update(definition)

        for key in schema:
            if isinstance(schema[key], dict):
                _resolveRef(schema[key], definitions)

        return schema

    global g_schema
    if not g_schema:
        with open(path) as f:
            schema = json.load(f)
            g_schema = _resolveRef(schema, schema['definitions'])

    return g_schema


def _createDirective(kvs, schema, innerkeys):
    def _createDefault(schema):
        if 'const' in schema:
            return schema['const']
        assert 'type' in schema, schema
        return JSON_TYPE_MAP[schema['type']][1]

    ret = {}
    for key in schema:
        if key in kvs:
            value = kvs[key]
        else:
            value = _createDefault(schema[key])
        ret[key] = value.copy() if isinstance(value, dict) else value

    for key in innerkeys:
        if key in kvs:
            value = kvs[key]
            ret[key] = value.copy() if isinstance(value, dict) else value

    return ret


def createMember(typeid, kvs):
    assert 'id' in kvs, kvs
    kvs['meta'] = 'Member'
    kvs['name'] = kvs['id']
    kvs['_typeid'] = typeid

    global g_schema
    schema = g_schema['definitions']['member']['properties']
    inner = ['_typeid', 'meta']
    return _createDirective(kvs, schema, inner)


def createTypeLocation(kvs):
    ids = BlockAssembler()
    ids.append(kvs['_env']['rng'])
    ids.append(kvs['_env']['define'] + '.define')
    if kvs['_nodepath']:
        nodepath = [n[0] + n[1] for n in kvs['_nodepath']]
        anchor = kvs.get('_anchor', -1)
        if anchor >= 0:
            nodepath[anchor] = '[%s]' % nodepath[anchor]
        ids.extend(nodepath)
    return '/' + ids.output('/')


def createFullname(kvs):
    if not kvs['_nodepath']:
        ret = kvs['_env']['define']
    else:
        ret = ''.join([Terms.upperInitial(n[0]) for n in kvs['_nodepath']])
    if not ret.startswith('vir'):
        ret = 'vir' + Terms.upperInitial(ret)
    return ret


def createType(meta, kvs, children=None):
    kvs['meta'] = meta
    if 'location' not in kvs:
        kvs['location'] = createTypeLocation(kvs)

    if meta in BUILTIN_TYPES:
        if 'name' in kvs and kvs['meta'] == 'String':
            kvs['gap'] = ' '    # Fix gap for hardcoded 'String'
    elif meta in ['Struct']:
        if 'name' not in kvs:
            kvs['name'] = createFullname(kvs) + 'Def'

        for kind in ['structure', 'clearfunc', 'parsefunc', 'formatfunc']:
            if kind not in kvs:
                kvs[kind] = {}

        for member in kvs.pop('members', {}):
            assert verifyMember(member), "Invalid member: %s" % member
            if 'type' in member:
                typeid = TypeTable().getByLocation(member['type'])['id']
                member['_typeid'] = typeid

            if 'id' not in member:
                member['hint'] = 'new'
                member['id'] = member['name']
                member['_env'] = kvs['_env']
                assert member['_typeid']
                children.append(createMember(member['_typeid'], member))
                continue

            child = findMember(member['id'], children)
            if child:
                deepupdate(child, member)
                mtype = TypeTable().get(child['_typeid'])
                if not child['hint']:
                    if mtype['unpack']:
                        child['hint'] = 'unpack'
                    elif mtype['pack']:
                        child['hint'] = 'pack'

        kvs['members'] = children
    elif meta in ['Enum']:
        if not kvs.get('name', None):
            fullname = createFullname(kvs)
            if not fullname.endswith('Type'):
                fullname += 'Type'
            kvs['name'] = fullname

        if 'structure' not in kvs:
            if 'structure' not in kvs:
                kvs['structure'] = {}

        if not kvs.get('values', None):
            kvs['values'] = children
    elif meta in ['Constant']:
        pass
    else:
        assert False, "Unsupported meta '%s'." % meta

    global g_schema
    schema = g_schema['properties']
    inner = ['_anchor', '_nodepath', '_env']
    return _createDirective(kvs, schema, inner)


def _verifyDirective(kvs, schema):
    def _verifyType(obj, schema):
        if 'const' in schema:
            return obj == schema['const']

        target = schema['type']
        if isinstance(target, list):
            for t in target:
                if type(obj).__name__ in JSON_TYPE_MAP[t][0]:
                    return True
            return False

        return type(obj).__name__ in JSON_TYPE_MAP[target][0]

    for key, value in kvs.items():
        if key.startswith('_'):
            continue
        if key not in schema:
            print("fatal: undefined directive '%s'" % key)
            return False
        if not _verifyType(value, schema[key]):
            print("fatal: directive '%s:%s' type error" % (key, value))
            return False
        if isinstance(value, dict):
            if not _verifyDirective(value, schema[key]['properties']):
                return False

    return True


def verifyMember(kvs):
    global g_schema
    schema = g_schema['definitions']['member']['properties']
    return _verifyDirective(kvs, schema)


def verifyType(kvs):
    global g_schema
    return _verifyDirective(kvs, g_schema['properties'])


BUILTIN_TYPES = {
    'PVoid': {'ctype': 'void *', 'gap': ''},
    'String': {'ctype': 'char *', 'gap': ''},
    'Bool': {'ctype': 'bool'},
    'Bool.yes_no': {'ctype': 'bool', 'values': ['yes', 'no']},
    'Bool.on_off': {'ctype': 'bool', 'values': ['yes', 'no']},
    'Chars': {
        'ctype': 'char', 'conv': 'virStrcpyStatic(def->${name}, ${name}Str)'
    },
    'UChars': {
        'ctype': 'unsigned char',
        'conv': 'virStrcpyStatic((char *)def->${name}, ${mdvar})'
    },
    'Int': {
        'ctype': 'int', 'fmt': '%d',
        'conv': 'virStrToLong_i(${mdvar}, NULL, 0, &def->${name})'
    },
    'UInt': {
        'ctype': 'unsigned int', 'fmt': '%u',
        'conv': 'virStrToLong_uip(${mdvar}, NULL, 0, &def->${name})'
    },
    'ULongLegacy': {
        'ctype': 'unsigned long', 'fmt': '%lu',
        'conv': 'virStrToLong_ulp(${mdvar}, NULL, 0, &def->${name})'
    },
    'ULong': {
        'ctype': 'unsigned long long', 'fmt': '%llu',
        'conv': 'virStrToLong_ullp(${mdvar}, NULL, 0, &def->${name})'
    },
    'U8': {
        'ctype': 'uint8_t', 'fmt': '%u',
        'conv': 'virStrToLong_u8p(${mdvar}, NULL, 0, &def->${name})'
    },
    'U32': {
        'ctype': 'uint32_t', 'fmt': '%u',
        'conv': 'virStrToLong_uip(${mdvar}, NULL, 0, &def->${name})'
    },
    'ConstString': {'ctype': 'const char *', 'gap': ''},
    'Constant': {'ctype': 'bool'},
}
BUILTIN_TYPES['Integer'] = BUILTIN_TYPES['Int']
BUILTIN_TYPES['UnsignedInt'] = BUILTIN_TYPES['UInt']
BUILTIN_TYPES['PositiveInteger'] = BUILTIN_TYPES['UInt']
BUILTIN_TYPES['UnsignedLong'] = BUILTIN_TYPES['ULong']


def isBuiltin(meta):
    return meta in BUILTIN_TYPES


class NodeList(list):
    def __init__(self, first=None):
        if first:
            self.append(first)

    def _getUniform(self, node):
        return 'Builtin' if isBuiltin(node['meta']) else node['meta']

    def uniform(self):
        return self._getUniform(self[0]) if len(self) else None

    def append(self, node):
        if len(self):
            assert self.uniform() == self._getUniform(node)

        if self.uniform() == 'Member':
            for cur in self:
                if cur['id'] == node['id'] and \
                        cur.get('more') == node.get('more'):
                    if cur['name'] == node['name']:
                        cur['opt'] = cur['opt'] or node['opt']
                        return
        elif self.uniform() == 'Builtin':
            cur = assertOnlyOne(self)
            if node['id'] == cur['id']:
                return

            # String is always swallowed by other builtin-types.
            if cur['meta'] == 'String' and node['meta'] != 'String':
                TypeTable().pop(cur['id'])
                self[0] = node
            else:
                TypeTable().pop(node['id'])
            return

        super(NodeList, self).append(node)

    def extend(self, nodes):
        if nodes:
            for node in nodes:
                self.append(node)


@singleton
class TypeTable(OrderedDict):
    def __init__(self):
        OrderedDict.__init__(self)
        for meta, kvs in BUILTIN_TYPES.items():
            tid = sha1ID(meta)
            kvs['id'] = tid
            kvs['location'] = meta
            self[tid] = createType(meta, kvs)

    def _merge(self, tid, newkvs):
        kvs = self[tid]
        if kvs['meta'] == 'Constant' and newkvs['meta'] == 'Constant':
            kvs['meta'] = 'Enum'    # Reset meta explicitly
            assert 'values' in kvs, kvs
            values = kvs.pop('values')
            values.extend(newkvs['values'])
            self[tid] = createType('Enum', kvs, values)
        elif kvs['meta'] == 'Enum' and newkvs['meta'] == 'Constant':
            kvs['values'].extend(newkvs['values'])
        elif kvs['meta'] == 'Struct' and newkvs['meta'] == 'Struct':
            kvs['members'].extend(newkvs['members'])
        else:
            assert isBuiltin(kvs['meta']) and isBuiltin(newkvs['meta']), \
                '%s:%s, %s' % (kvs['meta'], newkvs['meta'], tid)

    def register(self, meta, kvs, children=None):
        kvs = createType(meta, kvs, children)
        tid = sha1ID(kvs['location'])
        kvs['id'] = tid

        if tid in self:
            self._merge(tid, kvs)
        else:
            # Verify uniqueness of leftmost 8 digits of 'id'.
            assert not self._getByPartialID(tid[:8])
            self[tid] = kvs

        return tid

    def getByLocation(self, location):
        for _, atype in self.items():
            if atype.get('location', None) == location:
                return atype
        print("fatal: bad type location '%s'." % location)
        return None

    def _getByPartialID(self, pid):
        ret = []
        for key, atype in self.items():
            if key.startswith(pid):
                ret.append(atype)
        return ret

    def getByPartialID(self, pid):
        ret = self._getByPartialID(pid)
        if not ret:
            print("fatal: bad type id '%s'." % pid)
            return None
        elif len(ret) != 1:
            ids = ', '.join([item['id'][:8] for item in ret])
            print("notice: several candidates[%s] for id '%s'." % (ids, pid))
            return None

        return ret[0]


T_STRUCT_STRUCTURE = '''
typedef struct _${fullname} ${fullname};
typedef ${fullname} *${fullname}Ptr;
struct _${fullname} {
    ${members}
};
'''

T_ENUM_STRUCTURE_DECL = '''
typedef enum {
    ${caps_shortname}_${default} = 0,
    ${values}
    ${caps_shortname}_LAST,
} ${fullname};

VIR_ENUM_DECL(${shortname});
'''

T_ENUM_STRUCTURE_IMPL = '''
VIR_ENUM_IMPL(${shortname},
${indentation}${caps_shortname}_LAST,
${indentation}${array},
);
'''

T_MEMBER_DECL = '''
${type_decl}${gap}${asterisk}${name}${suffix};${comment}
'''

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


def pointer(atype):
    if isBuiltin(atype['meta']) and not atype.get('name', None):
        return BUILTIN_TYPES.get(atype['meta'])['ctype'] + '*'
    return atype['name'] + 'Ptr'


def proto(atype, pointer):
    if isBuiltin(atype['meta']) and not atype.get('name', None):
        return BUILTIN_TYPES.get(atype['meta'])['ctype']
    elif atype['meta'] == 'Struct' and pointer:
        return atype['name'] + 'Ptr'
    return atype['name']


def gapOf(atype):
    if isBuiltin(atype['meta']) and not atype.get('name', None):
        return BUILTIN_TYPES.get(atype['meta']).get('gap', ' ')
    return ' '


def declareMember(member):
    mtype = TypeTable().get(member['_typeid'])

    #
    # Helper functions
    #
    def _declare(type_decl, asterisk, gap, name):
        asterisk = '*' if asterisk else ''
        if mtype['meta'] in ['Chars', 'UChars']:
            suffix = '[%s]' % mtype['structure']['array.size']
        else:
            suffix = ''
        if member['declare.comment']:
            comment = ' /* %s */' % member['declare.comment']
        else:
            comment = ''
        return render(T_MEMBER_DECL, type_decl=type_decl,
                      gap=gap, asterisk=asterisk,
                      name=name, suffix=suffix, comment=comment)

    #
    # Main routine
    #
    code = ''
    if member['more']:
        code += 'size_t %s;\n' % counterName(member['name'])
        code += _declare(pointer(mtype), member['pointer'], gapOf(mtype),
                         Terms.pluralize(member['name']))
    else:
        code += _declare(proto(mtype, member.get('pointer', False)),
                         False, gapOf(mtype), member['name'])
        if member['specified']:
            code += '\nbool %s_specified;' % member['name']

    return code


def flattenMembers(members):
    ret = NodeList()
    for member in members:
        mtype = TypeTable().get(member['_typeid'])
        if mtype['meta'] == 'Struct' and mtype['unpack']:
            ret.extend(flattenMembers(mtype['members']))
        else:
            ret.append(member)

    return ret


def makeStructure(writer, atype):
    def _makeMembers():
        blocks = BlockAssembler()
        for member in flattenMembers(atype['members']):
            blocks.append(declareMember(member))

        if atype.get('namespace', False):
            blocks.append('void *namespaceData;')
            blocks.append('virXMLNamespace ns;')

        return blocks.output()

    if atype.get('unpack', False):
        writer.write(atype, 'structure', '.h', _makeMembers())
        return

    if atype['meta'] == 'Struct':
        members = indent(_makeMembers(), 1)
        decl = render(T_STRUCT_STRUCTURE,
                      fullname=atype['name'], members=members)

        writer.write(atype, 'structure', '.h', decl)
    elif atype['meta'] == 'Enum':
        assert atype['name'], atype
        shortname = atype['name'][:-4]
        caps_shortname = Terms.allcaps(shortname)
        default = atype['structure'].get('enum.default', 'none')
        atype['values'].insert(0, default)

        atype['_values_map'] = OrderedDict()
        for value in atype['values']:
            caps_value = Terms.allcaps(value).replace('.', '')
            item = '%s_%s' % (caps_shortname, caps_value)
            atype['_values_map'][value] = item

        values = ',\n'.join(list(atype['_values_map'].values())[1:]) + ','
        decl = render(T_ENUM_STRUCTURE_DECL,
                      shortname=shortname,
                      caps_shortname=Terms.allcaps(shortname),
                      fullname=atype['name'],
                      default=Terms.allcaps(default),
                      values=indent(values, 1))
        writer.write(atype, 'structure', '.h', decl)

        array = ', '.join(['"%s"' % v for v in atype['values']])
        impl = render(T_ENUM_STRUCTURE_IMPL,
                      shortname=shortname,
                      caps_shortname=Terms.allcaps(shortname),
                      indentation=align('VIR_ENUM_IMPL'),
                      default=default, array=array)
        writer.write(atype, 'structure', '.c', impl)


T_CLEAR_FUNC_IMPL = '''
void
${funcname}(${typename}Ptr def)
{
    if (!def)
        return;

    ${body}
}
'''

T_CLEAR_FUNC_DECL = '''
void
${funcname}(${typename}Ptr def);
'''


def clearMember(member):
    mtype = TypeTable().get(member['_typeid'])
    if member['more']:
        name = 'def->%s[i]' % Terms.pluralize(member['name'])
    else:
        name = 'def->%s' % member['name']

    funcname = mtype['clearfunc'].get('name', None)
    if not funcname and mtype['name']:
        funcname = mtype['name'] + 'Clear'

    code = ''
    if funcname and mtype['meta'] != 'Enum':
        amp = '' if member['pointer'] else '&'
        code = '%s(%s%s);' % (funcname, amp, name)
        if member['pointer']:
            code += '\nVIR_FREE(%s);' % name
    elif mtype['meta'] == 'String':
        code = 'VIR_FREE(%s);' % name
    elif mtype['meta'] in ['Chars', 'UChars']:
        code = 'memset(%s, 0, sizeof(%s));' % (name, name)
    elif not member['more']:
        code = '%s = 0;' % name

    if member['more']:
        if code:
            name = Terms.pluralize(member['name'])
            counter = counterName(member['name'])
            if singleline(code):
                code = render(T_LOOP_SINGLE, counter=counter, body=code)
            else:
                code = render(T_LOOP_MULTI,
                              counter=counter, body=indent(code, 2))
            code += '\nVIR_FREE(def->%s);\ndef->%s = 0;' % (name, counter)
    else:
        if member['specified']:
            code += '\n%s_specified = false;' % name

    return code


T_CLEAR_NAMESPACE = '''
if (def->namespaceData && def->ns.free)
    (def->ns.free)(def->namespaceData);
'''


def makeClearFunc(writer, atype):
    clearfunc = atype['clearfunc']

    if atype['unpack']:
        return

    blocks = BlockAssembler()
    for member in flattenMembers(atype['members']):
        blocks.append(clearMember(member))

    funcname = clearfunc.get('name', None)
    if not funcname:
        funcname = atype['name'] + 'Clear'

    if atype.get('namespace', False):
        blocks.append(T_CLEAR_NAMESPACE.strip())

    impl = render(T_CLEAR_FUNC_IMPL, funcname=funcname, typename=atype['name'],
                  body=indent(blocks.output('\n\n'), 1))

    decl = render(T_CLEAR_FUNC_DECL, funcname=funcname, typename=atype['name'])

    writer.write(atype, 'clearfunc', '.h', decl)
    writer.write(atype, 'clearfunc', '.c', impl)


#
# Templates for parsing member block
#
T_SET_DEFAULT_VALUE = 'def->${name} = ${default};'
T_READ_XML_BY_XPATH = '${mdvar} = ${xfuncname}(${xpath}, ctxt);'
T_READ_ATTR_BY_PROP = '${mdvar} = virXMLPropString(curnode, "${oname}");'
T_READ_ELEM_BY_PROP = '${mdvar} = virXMLChildNode(curnode, "${oname}");'

T_READ_NODES = '${number} = virXMLChildNodeSet(curnode, "${oname}", &nodes);'
T_READ_NODES_CTXT = '${number} = virXPathNodeSet("./${oname}", ctxt, &nodes);'

T_PARSE_MEMBER_MORE = '''
if (${number} > 0) {
    size_t i;
    xmlNodePtr node;

    if (VIR_ALLOC_N(def->${name}, ${number}) < 0)
        goto error;

    for (i = 0; i < ${number}; i++) {
        node = nodes[i];
        ${item}
    }
    def->${counter} = ${number};
    VIR_FREE(nodes);
} else if (${number} < 0) {
    virReportError(VIR_ERR_XML_ERROR, "%s",
                   _("Invalid ${oname} element found."));
    goto error;
}${report_missing}
'''

T_GENERATE_ON_MISSING = '''
if (${funcname}(def->${name}) < 0) {
    virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                   _("cannot generate a random uuid for ${name}"));
    goto error;
}
'''

T_REPORT_INVALID_WITH_INSTANCE = '''
    virReportError(VIR_ERR_XML_ERROR,
                   _("Invalid '${oname}' setting '%s' in '%s'"),
                   ${mdvar}, instanceName);
'''

T_REPORT_INVALID_WITHOUT_INSTANCE = '''
    virReportError(VIR_ERR_XML_ERROR,
                   _("Invalid '${oname}' setting '%s'"),
                   ${mdvar});
'''

T_CHECK_INVALID_ERROR = '''
if (${tmpl}) {
    ${report_err}
    goto error;
}
'''

T_REPORT_MISSING_WITH_INSTANCE = '''
    virReportError(VIR_ERR_XML_ERROR,
                   _("Missing '${oname}' setting in '%s'"),
                   instanceName);
'''

T_REPORT_MISSING_WITHOUT_INSTANCE = '''
    virReportError(VIR_ERR_XML_ERROR, "%s",
                   _("Missing '${oname}' setting"));
'''

T_MISSING_ERROR = '''
{
    ${report_err}
    goto error;
}
'''

T_CHECK_MISSING_ERROR = 'if (${mdvar} == NULL) ' + T_MISSING_ERROR.strip()

T_ALLOC_MEMORY = '''
if (VIR_ALLOC(def->${name}) < 0)
    goto error;
'''

T_STRUCT_ASSIGNMENT_TEMPLATE = '''
if (${funcname}(${mdvar}, ${amp}${refname}${args}) < 0)
    goto error;
'''

T_POST_PARSE_MEMBER = '''
if (${funcname}Post(name, ${amp}def->${name}, NULL) < 0)
    goto error;
'''


def makeActualArgs(formal_args, actual_args):
    def _findValue(name):
        if actual_args:
            for arg in actual_args:
                if arg['name'] == name:
                    return arg['value']
        return None

    if not formal_args:
        return []

    args = []
    for arg in formal_args:
        value = _findValue(arg['name'])
        if value:
            args.append(value)
        elif arg.get('pointer'):
            args.append(arg['name'])
        else:
            assert arg.get('type'), arg
            argtype = TypeTable().getByLocation(arg['type'])
            if argtype['meta'].startswith('Bool'):
                args.append('false')
            else:
                args.append('0')

    return args


def parseMember(member, atype, tmpvars, pack=None):
    if member['parse.disable']:
        return None

    mtype = TypeTable().get(member['_typeid'])

    if mtype['pack'] or mtype['union']:
        block = BlockAssembler()
        for child in mtype['members']:
            block.append(parseMember(child, atype, tmpvars, member))
        return block.output('\n\n')

    #
    # Helper functions
    #
    def _makeXPath():
        if member['tag'] == 'attribute':
            return '"string(./@%s)"' % member['id']

        if mtype['meta'] == 'Struct':
            return '"./%s"' % member['id']
        return '"string(./%s[1])"' % member['id']

    def _reportInvalid(mdvar):
        if atype['parsefunc'] and atype['parsefunc'].get('args.instname'):
            return render(T_REPORT_INVALID_WITH_INSTANCE,
                          oname=member['id'], mdvar=mdvar)
        return render(T_REPORT_INVALID_WITHOUT_INSTANCE,
                      oname=member['id'], mdvar=mdvar)

    def _reportMissing():
        if atype['parsefunc'] and atype['parsefunc'].get('args.instname'):
            return render(T_REPORT_MISSING_WITH_INSTANCE,
                          oname=member['id'])
        return render(T_REPORT_MISSING_WITHOUT_INSTANCE,
                      oname=member['id'])

    def _setDefaultValue(name):
        if not member['parse.default']:
            return None
        return render(T_SET_DEFAULT_VALUE,
                      name=name, default=member['parse.default'])

    def _readXMLByXPath(mdvar, xleaf):
        tag = member['tag']
        if not tag:
            return 'node = ctxt->node;'

        if tag == 'attribute':
            return render(T_READ_ATTR_BY_PROP,
                          mdvar=mdvar, oname=member['id'])

        if tag == 'element' and mtype['meta'] == 'Struct':
            if mtype['parsefunc'].get('args.noctxt', False):
                return render(T_READ_ELEM_BY_PROP,
                              mdvar=mdvar, oname=member['id'])

        if mtype['meta'] in ['Struct']:
            xfuncname = 'virXPathNode'
        else:
            xfuncname = 'virXPathString'
        return render(T_READ_XML_BY_XPATH,
                      mdvar=mdvar, xfuncname=xfuncname, xpath=xleaf)

    def _assignValue(name, mdvar):
        refname = 'def->' + name
        if mtype['unpack'] and not pack:
            refname = 'def'

        if mtype['meta'] in ['Struct']:
            funcname = mtype['parsefunc'].get('name', None)
            if not funcname:
                funcname = mtype['name'] + 'ParseXML'

            tmpl = ''
            if member['pointer']:
                tmpl += T_ALLOC_MEMORY
            tmpl += T_STRUCT_ASSIGNMENT_TEMPLATE
            if member.get('parse.post', False):
                tmpl += T_POST_PARSE_MEMBER

            args = []
            if not mtype['parsefunc'].get('args.noctxt'):
                args.append('ctxt')
            if mtype['parsefunc'].get('args.instname'):
                if member['parse.instname']:
                    args.append(member['parse.instname'])
                else:
                    args.append('instanceName')
            if mtype['parsefunc'].get('args.parent'):
                args.append('def')

            args.extend(makeActualArgs(mtype['parsefunc'].get('args'),
                                       member['parse.args']))
            args = ', '.join(args)
            if args:
                args = ', ' + args

            if refname == 'def' or member['pointer']:
                amp = ''
            else:
                amp = '&'

            tmpl = render(tmpl, funcname=funcname, args=args,
                          alignment=align('if (' + funcname),
                          amp=amp, mdvar=mdvar)
        elif mtype['meta'] == 'Enum':
            tmpl = '(def->${name} = %sFromString(%s)) <= 0' \
                % (mtype['name'], mdvar)
        elif mtype['meta'] == 'Constant' and mtype['values'][0] == 'yes' \
                or mtype['meta'] == 'Bool.yes_no':
            tmpl = 'virStringParseYesNo(${mdvar}, &def->${name}) < 0'
        elif mtype['meta'] == 'Constant' and mtype['values'][0] == 'on' \
                or mtype['meta'] == 'Bool.on_off':
            tmpl = 'virStringParseOnOff(${mdvar}, &def->${name}) < 0'
        elif mtype['meta'] == 'Constant' or mtype['meta'] == 'Bool':
            tmpl = 'virStrToBool(${mdvar}, "%s", &def->${name}) < 0' \
                % mtype['values'][0]
        elif mtype['name'] or mtype['parsefunc'].get('name'):
            funcname = mtype['parsefunc'].get('name', None)
            if not funcname:
                funcname = mtype['name'] + 'ParseXML'
            if mtype['meta'] in ['UChars', 'Chars']:
                aof = ''
            else:
                aof = '&'
            tmpl = '%s(${mdvar}, %sdef->${name}) < 0' % (funcname, aof)
        elif mtype['meta'] == 'String':
            tmpl = 'def->${name} = g_strdup(${mdvar});'
        else:
            tmpl = None
            builtin = BUILTIN_TYPES.get(mtype['meta'], None)
            if builtin:
                tmpl = builtin.get('conv', None)
                if tmpl:
                    tmpl += ' < 0'

        if not tmpl:
            return None

        if mdvar.endswith('Str') and \
                (mtype['meta'] != 'String' or mtype['name']):
            tmpl = render(T_CHECK_INVALID_ERROR,
                          tmpl=tmpl, report_err=_reportInvalid(mdvar))

        ret = render(tmpl, refname=refname, name=name,
                     oname=member['id'], mdvar=mdvar)

        if member['specified'] and not member['more']:
            ret += '\ndef->%s_specified = true;' % name
        return ret

    def _assignValueOnCondition(name, mdvar):
        block = _assignValue(name, mdvar)
        if not block:
            return None

        ret = None
        if member['opt']:
            if singleline(block):
                ret = render(T_IF_CONDITION_SINGLE, condition=mdvar,
                             body=block)
            else:
                ret = render(T_IF_CONDITION_MULTI, condition=mdvar,
                             body=indent(block, 1))
        else:
            ret = render(T_CHECK_MISSING_ERROR,
                         mdvar=mdvar, report_err=_reportMissing())
            if block:
                ret += '\n\n' + block
        return ret

    #
    # Main routine
    #
    if not member['tag'] or member['id'] == '_Any_':
        return None

    # For sequence-type member
    if member['more']:
        assert member['tag'] == 'element'
        node_num = 'n%sNodes' % Terms.upperInitial(member['id'])
        tmpvars.append(node_num)
        tmpvars.append('nodes')

        if pack:
            seqname = Terms.pluralize(pack['name'])
            counter = counterName(pack['name'])
        else:
            seqname = Terms.pluralize(member['name'])
            counter = counterName(member['name'])

        name = seqname + '[i]'
        report_missing = ''
        if not member['opt']:
            report_missing = ' else ' + render(T_MISSING_ERROR,
                                               report_err=_reportMissing())

        if mtype['meta'] != 'Struct':
            item = 'def->%s = virXMLNodeContentString(node);' % name
        else:
            item = _assignValue(name, 'node')

        if atype['parsefunc'] and atype['parsefunc'].get('args.noctxt'):
            tmpl = T_READ_NODES
        else:
            tmpl = T_READ_NODES_CTXT
        tmpl += T_PARSE_MEMBER_MORE
        return render(tmpl, name=seqname, counter=counter,
                      number=node_num, item=indent(item, 2),
                      report_missing=report_missing, oname=member['id'])

    # For ordinary member
    if pack:
        arrow = '->' if pack['pointer'] else '.'
        name = pack['name'] + arrow + member['name']
    else:
        name = member['name']

    blocks = BlockAssembler()
    mdvar = member['name']
    mdvar += 'Node' if mtype['meta'] in ['Struct'] else 'Str'
    tmpvars.append(mdvar)
    xpath = _makeXPath()

    blocks.append(_setDefaultValue(name))
    blocks.append(_readXMLByXPath(mdvar, xpath))
    blocks.append(_assignValueOnCondition(name, mdvar))
    return blocks.output()


def align(funcname):
    return ' ' * (len(funcname) + 1)


T_PARSE_FUNC_DECL = '''
int
${funcname}(${formal_args});
'''

T_PARSE_FUNC_IMPL = '''
int
${funcname}(${formal_args})
{
    ${declare_vars}

    ${body}

    ${end}

 error:
    ${cleanup_vars}
    ${typename}Clear(def);
    return -1;
}
'''

T_PARSE_FUNC_POST_INVOKE = '''
if (${funcname}Post(${actual_args}) < 0)
    goto error;
'''

T_FUNC_EXTRA_ARGS = '''
${alignment}${ctype}${gap}${name}
'''


def _handleTmpVars(tmpvars, noctxt):
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

    if not noctxt:
        heads.append('xmlNodePtr save = ctxt->node;')
        heads.append('ctxt->node = curnode;')
        tails.insert(0, 'ctxt->node = save;')
    return '\n'.join(heads), '\n'.join(tails)


def findMember(mid, members):
    members = list(filter(lambda m: m['id'] == mid, members))
    if not members:
        return None
    return assertOnlyOne(members)


def makeParseFunc(writer, atype):
    if atype['pack'] or atype['union']:
        return

    parsefunc = atype['parsefunc']
    funcname = parsefunc.get('name', None)
    if not funcname:
        funcname = atype['name'] + 'ParseXML'

    alignment = align(funcname)

    if atype['unpack']:
        if not atype.get('_parent'):
            print("fatal: unpack is set on direct child(%s) of <define>."
                  % atype['location'])
            sys.exit(-1)
        typename = atype['_parent']['name']
    else:
        typename = atype['name']

    formal_args = ['xmlNodePtr curnode', typename + 'Ptr def']
    actual_args = ['curnode', 'def']

    if not parsefunc.get('args.noctxt', False):
        formal_args.append('xmlXPathContextPtr ctxt')
        actual_args.append('ctxt')

    if parsefunc.get('args.instname'):
        formal_args.append('const char *instanceName')
        actual_args.append('instanceName')

    if parsefunc.get('args.parent'):
        assert atype.get('_parent')
        formal_args.append('%sPtr parentdef' % atype['_parent']['name'])
        actual_args.append('parentdef')

    if atype.get('namespace'):
        formal_args.append('virNetworkXMLOptionPtr xmlopt')
        actual_args.append('xmlopt')

    formal_args.extend(createFormalArgs(parsefunc.get('args'), alignment))
    actual_args.extend([arg['name'] for arg in parsefunc.get('args', [])])

    kwargs = {'funcname': funcname, 'typename': typename,
              'formal_args': (',\n%s' % alignment).join(formal_args)}

    tmpvars = []
    blocks = BlockAssembler()
    for member in atype['members']:
        blocks.append(parseMember(member, atype, tmpvars))

    decl = renderByDict(T_PARSE_FUNC_DECL, kwargs)

    if parsefunc.get('post', False):
        if not parsefunc.get('post.notmpvars', False):
            for var in tmpvars:
                if var.endswith('Str') or var.endswith('Node') or \
                        var.endswith('Nodes') and var.startswith('n'):
                    actual_args.append(var)

        actual_args = ', '.join(actual_args) if actual_args else ''
        post = render(T_PARSE_FUNC_POST_INVOKE, funcname=funcname,
                      actual_args=actual_args)
        blocks.append(post)

        if not parsefunc.get('post.notmpvars', False):
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
        decl += '\n' + render(T_PARSE_FUNC_DECL, funcname=funcname + 'Post',
                              formal_args=connector.join(formal_args))

    writer.write(atype, 'parsefunc', '.h', decl)

    if atype['namespace']:
        blocks.append(T_NAMESPACE_PARSE.strip())

    kwargs['body'] = indent(blocks.output('\n\n'), 1)

    declare_vars, cleanup_vars = _handleTmpVars(tmpvars,
                                                parsefunc.get('args.noctxt'))
    kwargs['declare_vars'] = indent(declare_vars, 1)
    kwargs['cleanup_vars'] = indent(cleanup_vars, 1)

    end = ''
    if not parsefunc.get('args.noctxt', False):
        end = 'ctxt->node = save;\n'
    end += 'return 0;'

    kwargs['end'] = indent(end, 1)

    impl = renderByDict(T_PARSE_FUNC_IMPL, kwargs)
    writer.write(atype, 'parsefunc', '.c', impl)


T_FORMAT_FUNC_DECL = '''
int
${funcname}(${formal_args});
'''

T_FORMAT_FUNC_IMPL = '''
int
${funcname}(${formal_args})
{
    if (!def)
        return 0;

    ${format_members}

    return 0;
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

T_IF_CONDITION_SINGLE = '''
if (${condition})
    ${body}
'''

T_IF_CONDITION_MULTI = '''
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
                   _("Unknown ${oname} type %d"),
                   ${var});
    return -1;
}
virBufferAsprintf(buf, "${layout}", str);
'''

T_FORMAT_PRECHECK_DECLARE = '''
bool
${funcname}(${formal_args});
'''


def createFormalArgs(args, alignment):
    if not args:
        return []

    lines = []
    for arg in args:
        gap = ' '
        ctype = arg.get('ctype', None)
        if not ctype:
            argtype = TypeTable().getByLocation(arg['type'])
            if isBuiltin(argtype['meta']):
                ctype = BUILTIN_TYPES.get(argtype['meta'])['ctype']
                gap = gapOf(argtype)
            else:
                assert argtype['meta'] in ['Struct']
                ctype = argtype['name']

        name = arg['name']
        if arg.get('pointer', False):
            name = '*' + name

        line = render(T_FUNC_EXTRA_ARGS, alignment=alignment,
                      ctype=ctype, name=name, gap=gap)
        lines.append(line)

    return lines


def formatMember(member, require, ret_checks, ret_decls, atype, pack=None):
    if member['format.disable']:
        return None

    mtype = TypeTable().get(member['_typeid'])

    if mtype['pack'] or mtype['union']:
        checks = []
        block = BlockAssembler()
        for child in mtype['members']:
            block.append(formatMember(child, require, checks, ret_decls,
                                      atype, member))
        checks = dedup(checks)
        ret_checks.append(' || '.join(checks))
        return block.output('\n\n')

    #
    # Helper functions.
    #
    def _checkForMember(m, var):
        t = TypeTable().get(m['_typeid'])

        ret = None
        if m['pointer']:
            ret = var
        elif m['specified']:
            ret = var + '_specified'
            if ret.startswith('&'):
                ret = ret[1:]
        elif t['meta'] in ['Chars', 'UChars']:
            ret = var + '[0]'
        elif t['meta'] == 'Enum':
            ret = var
        elif isBuiltin(t['meta']):
            ret = var
            if t['name']:
                ret = '%sCheck(%s)' % (t['name'], var)

        return ret

    def _makeVar():
        if mtype['unpack'] and not pack:
            return 'def'

        curname = member['name']
        if member['more']:
            curname = Terms.pluralize(member['name']) + '[i]'

        if pack:
            packname = pack['name']
            if pack['more']:
                packname = Terms.pluralize(pack['name']) + '[i]'
            if pack['hint'] == 'union':
                var = packname
            else:
                arrow = '->' if pack['pointer'] else '.'
                var = packname + arrow + curname
        else:
            var = curname
        var = 'def->' + var

        if mtype['meta'] == 'Struct':
            if not member['pointer']:
                var = '&' + var
        elif mtype['meta'] != 'Enum' and mtype['name']:
            var = '&' + var

        return var

    def _checkOnCondition(var):
        if member['format.nocheck']:
            return None

        if member.get('format.precheck'):
            alist = [a['name'] for a in atype['formatfunc'].get('args', [])]
            args = ', '.join(alist)
            if args:
                args = ', ' + args

            precheck = member['format.precheck']
            checks = render('${funcname}(${var}, def${args})',
                            funcname=precheck, var=var, args=args)

            ret_decls.append(_makePrecheckDeclare(precheck, var))
            return checks

        if mtype['unpack']:
            return mtype.get('check_all_members', None)

        if member['more']:
            return None

        checks = _checkForMember(member, var)
        return checks

    def _handleMore(code):
        code = indent(code, 2)
        if pack:
            counter = counterName(pack['name'])
        else:
            counter = counterName(member['name'])
        ret_checks.append('def->' + counter)
        if singleline(code):
            return render(T_LOOP_SINGLE, counter=counter, body=code)

        return render(T_LOOP_MULTI, counter=counter, body=code)

    def _makePrecheckDeclare(funcname, var):
        assert funcname
        if mtype['unpack']:
            typename = mtype['_parent']['name']
        else:
            typename = proto(mtype, False)

        if atype['unpack']:
            parentname = atype['_parent']['name']
        else:
            parentname = proto(atype, False)

        asterisk = '*' if mtype['unpack'] or var.startswith('&') else ''
        args = ['const %s %sdef' % (typename, asterisk)]
        args.append('const %s *parent' % parentname)
        args.extend(createFormalArgs(atype['formatfunc'].get('args'),
                                     align(funcname)))
        fargs = (',\n%s' % align(funcname)).join(args)
        return render(T_FORMAT_PRECHECK_DECLARE,
                      funcname=funcname, formal_args=fargs)

    def _format(layout, var):
        tmpl = '${funcname}(buf, "${layout}", ${var}${args})'

        args = []
        funcname = 'virBufferAsprintf'
        has_return = False
        if mtype['meta'] == 'Struct':
            if mtype['formatfunc'].get('name', None):
                funcname = mtype['formatfunc']['name']
            else:
                funcname = mtype['name'] + 'FormatBuf'

            args.extend(makeActualArgs(mtype['formatfunc'].get('args'),
                                       member['format.args']))
            has_return = True
        elif mtype['meta'] == 'Enum':
            tmpl = render(T_FORMAT_MEMBER_OF_ENUM,
                          fullname=mtype['name'],
                          oname=member['id'])
        elif mtype['name'] or mtype['formatfunc'].get('name', None):
            if mtype['formatfunc'].get('name', None):
                funcname = mtype['formatfunc']['name']
            else:
                funcname = mtype['name'] + 'FormatBuf'
            has_return = True
        elif mtype['meta'] in ['String', 'Chars', 'UChars']:
            funcname = 'virBufferEscapeString'
        elif mtype['meta'] == 'Bool.yes_no':
            var = '%s ? "yes" : "no"' % var
        elif mtype['meta'] == 'Bool.on_off':
            var = '%s ? "on" : "off"' % var
        elif mtype['meta'] == 'Bool':
            pass
        elif mtype['meta'] == 'Constant':
            tmpl = 'virBufferAddLit(buf, "${layout}")'
            layout = " %s='%s'" % (member['id'], mtype['values'][0])

        args = ', '.join(args)
        if args:
            args = ', ' + args

        code = render(tmpl, funcname=funcname, layout=layout,
                      var=var, args=args)
        if has_return:
            code += ' < 0'
            code = render(T_IF_CONDITION_SINGLE,
                          condition=code, body='return -1;')
        elif mtype['meta'] not in ['Enum']:
            code += ';'

        return code

    def _handleAttr(tagname, var):
        if member['tag'] != 'attribute':
            return None

        fmt = '%s'
        if member['format.fmt']:
            fmt = member['format.fmt']
        elif isBuiltin(mtype['meta']):
            fmt = BUILTIN_TYPES[mtype['meta']].get('fmt', '%s')

        layout = " %s='%s'" % (tagname, fmt)
        return _format(layout, var)

    def _handleElem(tagname, var):
        if member['tag'] == 'attribute':
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
    assert require in ['attribute', 'element']
    if not member.get('tag', None):
        return None

    var = _makeVar()

    ret = None
    tagname = member['id']
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
            ret = render(T_IF_CONDITION_SINGLE,
                         condition=checks, body=ret)
        else:
            ret = render(T_IF_CONDITION_MULTI,
                         condition=checks, body=ret)

    if member['more']:
        return _handleMore(ret)

    if checks:
        if '&&' in checks or '||' in checks:
            checks = '(%s)' % checks
        ret_checks.append(checks)

    return ret


def makeFormatFunc(writer, atype):
    formatfunc = atype['formatfunc']

    #
    # Helper functions.
    #
    def _reorder(children, order):
        if not order:
            return children

        ret = NodeList()
        for mid in order:
            ret.append(findMember(mid, children))

        if len(order) < len(children):
            for child in children:
                if child['id'] not in order:
                    ret.append(child)

        return ret

    def _formatMembers(prechecks):
        attrs = []
        elems = []
        check_attrs = []
        check_elems = []
        members = _reorder(atype['members'], formatfunc.get('order', None))

        for member in members:
            attr = formatMember(member, 'attribute',
                                check_attrs, prechecks, atype)
            if attr:
                attrs.append(attr)

            elem = formatMember(member, 'element',
                                check_elems, prechecks, atype)
            if elem:
                elems.append(elem)

        ret = BlockAssembler()
        if len(check_attrs) == len(attrs) \
                and len(check_elems) == len(elems):
            checks = ' || '.join(check_attrs + check_elems)
            atype['check_all_members'] = checks
            ret.append(render(T_IF_CONDITION_SINGLE,
                              condition='!(%s)' % checks,
                              body='return 0;'))

        ret.append('virBufferAsprintf(buf, "<%s", name);')

        if atype['namespace']:
            ret.append(T_NAMESPACE_FORMAT_BEGIN.strip())

        ret.extend(attrs)

        if elems:
            if not formatfunc.get('shorthand.ignore'):
                if attrs and len(check_elems) == len(elems):
                    checks = ' || '.join(check_elems)
                    ret.append(render(T_FORMAT_SHORTHAND, checks=checks))

            elements = '\n\n'.join(elems)
            if atype['namespace']:
                elements += '\n\n' + T_NAMESPACE_FORMAT_END.strip()

            ret.append(render(T_FORMAT_ELEMENTS, elements=elements))
        else:
            ret.append('virBufferAddLit(buf, "/>\\n");')

        return ret.output('\n\n')

    #
    # Main routine of formating.
    #
    if atype['pack'] or atype['union']:
        return

    if formatfunc.get('name', None):
        funcname = formatfunc['name']
    else:
        funcname = atype['name'] + 'FormatBuf'

    if atype['unpack']:
        typename = atype['_parent']['name']
    else:
        typename = atype['name']

    alignment = align(funcname)

    args = [{'name': 'buf', 'ctype': 'virBufferPtr'},
            {'name': 'name', 'ctype': 'const char', 'pointer': True},
            {'name': 'def', 'ctype': 'const ' + typename, 'pointer': True}]

    args.extend(formatfunc.get('args', []))

    formal_args = createFormalArgs(args, alignment)
    formal_args = (',\n%s' % alignment).join(formal_args)

    kwargs = {'funcname': funcname, 'formal_args': formal_args}

    prechecks = []
    format_members = _formatMembers(prechecks)

    decl = renderByDict(T_FORMAT_FUNC_DECL, kwargs)
    if prechecks:
        prechecks = dedup(prechecks)
        decl += '\n\n' + '\n\n'.join(prechecks)
    writer.write(atype, 'formatfunc', '.h', decl)

    kwargs['format_members'] = indent(format_members, 1)

    impl = renderByDict(T_FORMAT_FUNC_IMPL, kwargs)
    writer.write(atype, 'formatfunc', '.c', impl)


T_DIRECTIVE_JSON = '''
<!-- VIRT:DIRECTIVE {
  ${items}
} -->
'''


def dumpJson(atype):
    def _dumpJson(obj, compact=False):
        lines = BlockAssembler()
        for key, value in obj.items():
            if key.startswith('_') or key in ['tag', 'output']:
                continue
            if not value:
                continue
            if key == 'name' and value == obj['id']:
                continue
            if key == 'meta' and value == 'Member':
                mtype = TypeTable().get(obj['_typeid'])
                desc = '%s:%s' % (mtype['meta'], mtype['id'][:8])
                lines.append('"type": "%s"' % desc)
                continue
            if key == 'members':
                block = BlockAssembler()
                for member in value:
                    block.append('  ' + _dumpJson(member, True))
                lines.append('"members": [\n%s\n]' % block.output(',\n'))
                continue
            if key in ['_env']:
                value = _dumpJson(value, True)
            else:
                value = json.dumps(value)
            lines.append('"%s": %s' % (key, value))

        if compact:
            return '{' + lines.output(', ') + '}'
        return lines.output(',\n')

    return _dumpJson(atype)


def showDirective(atype):
    print('\n###### Directive ######\n')
    items = indent(dumpJson(atype), 1, 2)
    print(render(T_DIRECTIVE_JSON, items=items))
