
'''
pip install doxmlparser
cd zephyr/doc
rm -Rf _build
make configure
cd _build
ninja doxygen
'''

from json import JSONEncoder
from pathlib import Path
from typing import Any
from nodes import Enum, EnumValue, File, Function, FunctionLike, Group, Node, Param, Struct, StructField, Typedef, Variable, Define
from dox_parser import ParseResult, dump_doxygen_json, parse_doxygen, save_doxygen
import enum

HEADER_FILE_EXTENSION = '.h'

XML_DIR = Path('zephyr/doc/_build/doxygen/xml')

#res = parse_doxygen(XML_DIR)
#res = parse_doxygen(Path('../a.pkl'))
#save_doxygen(res, '../new.pkl'); dump_doxygen_json(res, '../new.json')
#save_doxygen(res, '../old.pkl'); dump_doxygen_json(res, '../old.json')


def convert_to_long_key(group: 'dict[None]') -> dict[None]:
    result = {}
    for group_key, group_node in group.items():
        result[group_key + '>' + group_node.id + '>' + str(group_node.line)] = group_node
    return result


def match_groups(matched: 'list[tuple[Node, Node]]', added: 'list[Node]', old_matched: 'set[Node]', new_group: 'dict[None]', old_group: 'dict[None]'):
    new_is_long_key = tuple(new_group.keys())[0].count('>') > 0
    old_is_long_key = tuple(old_group.keys())[0].count('>') > 0
    if new_is_long_key and not old_is_long_key:
        old_group = convert_to_long_key(old_group)
    elif old_is_long_key and not new_is_long_key:
        new_group = convert_to_long_key(new_group)

    for key, new_node in new_group.items():
        if key in old_group:
            old_node = old_group[key]
            matched.append((new_node, old_node))
            old_matched.add(old_node)
        else:
            added.append(new_node)


def on_delete(node: Node):
    print(f"Deleted {node.name} from {node.file}:{node.line}")

def on_added(node: Node):
    print(f"Added {node.name} from {node.file}:{node.line}")

def on_kind_changed(new: Node, old: Node):
    print(f"Change kind {new.name} from {new.file}:{new.line}")
    print(f"            {old.name} from {old.file}:{old.line}")

def on_desc_changed(new: Node, old: Node):
    print(f"Change desc {new.name} from {new.file}:{new.line}")
    print(f"            {old.name} from {old.file}:{old.line}")

def on_file_changed(new: Node, old: Node):
    print(f"Change file {new.name} from {new.file}:{new.line}")
    print(f"            {old.name} from {old.file}:{old.line}")


def match_items(new: 'list[EnumValue | Param | StructField]', old: 'list[EnumValue | Param | StructField]') -> 'tuple[list[EnumValue | Param | StructField], list[tuple[EnumValue | Param | StructField, EnumValue | Param | StructField]], list[EnumValue | Param | StructField]]':
    def by_name(items: 'list[EnumValue | Param | StructField]'):
        result = {}
        for item in items:
            if item.name not in result:
                result[item.name] = item
        return result

    new_by_name = by_name(new)
    old_by_name = by_name(old)

    deleted = set(old_by_name.values())
    matched = []
    added = []

    for name, new_value in new_by_name.items():
        if name in old_by_name:
            matched.append((new_value, old_by_name[name]))
            if old_by_name[name] in deleted:
                deleted.remove(old_by_name[name])
        else:
            added.append(new_value)

    deleted = list(deleted)

    return deleted, matched, added

ADDED = 'added'
DELETED = 'deleted'
MODIFIED = 'modified'

class AnyChange:
    action: str
    def __init__(self, action: str, new: Any, old: Any):
        self.action = action
        self.new = new
        self.old = old

class KindChange(AnyChange):
    kind = 'kind'
    new: Any
    old: Any

class NodeChange(AnyChange):
    file: bool = False
    desc: bool = False

class SimpleNodeChange(NodeChange):
    type: bool = False

class TypedefChange(SimpleNodeChange):
    kind = 'typedef'
    new: Typedef
    old: Typedef

class VariableChange(SimpleNodeChange):
    kind = 'var'
    new: Variable
    old: Variable

class EnumValueChange(AnyChange):
    kind = 'enum_value'
    index: bool = False
    value: bool = False
    desc: bool = False
    new: EnumValue
    old: EnumValue

class EnumChange(NodeChange):
    kind = 'enum'
    values: 'list[EnumValueChange]'
    new: Variable
    old: Variable
    def __init__(self, action: str, new: Any, old: Any):
        super().__init__(action, new, old)
        self.values = []

class StructFieldChange(AnyChange):
    kind = 'field'
    index: bool = False
    type: bool = False
    desc: bool = False
    new: StructField
    old: StructField

class StructChange(NodeChange):
    kind = 'struct'
    fields: 'list[StructFieldChange]'
    new: Variable
    old: Variable
    def __init__(self, action: str, new: Any, old: Any):
        super().__init__(action, new, old)
        self.fields = []


class ParamChange(AnyChange):
    kind = 'param'
    index: bool = False
    name: bool = False
    type: bool = False
    desc: bool = False
    new: Param
    old: Param

class FunctionLikeChange(NodeChange):
    params: 'list[ParamChange]'
    def __init__(self, action: str, new: Any, old: Any):
        super().__init__(action, new, old)
        self.params = []

class FunctionChange(FunctionLikeChange):
    kind: str = 'func'
    return_type: bool = False
    new: Function
    old: Function

class DefineChange(FunctionLikeChange):
    kind: str = 'def'
    value: bool = False
    new: Define
    old: Define

def get_add_delete(node: Node, action: str) -> 'AnyChange':
    if isinstance(node, Typedef):
        return TypedefChange(action, node, node)
    elif isinstance(node, Variable):
        return VariableChange(action, node, node)
    elif isinstance(node, Enum):
        return EnumChange(action, node, node)
    elif isinstance(node, Struct):
        return StructChange(action, node, node)
    elif isinstance(node, Function):
        return FunctionChange(action, node, node)
    elif isinstance(node, Define):
        return DefineChange(action, node, node)

def get_changes(new: Node, old: Node) -> 'list[AnyChange]':
    result:'list[AnyChange]' = []

    if (new.kind != old.kind) or (type(new) != type(old)):
        return [KindChange(MODIFIED, new, old)]
    if isinstance(new, File) or isinstance(new, Group):
        return []

    node_change = None
    updated = False

    if isinstance(new, Typedef):
        new: Typedef
        old: Typedef
        node_change = TypedefChange(MODIFIED, new, old)
        node_change.type = (new.type != old.type)
        updated = node_change.type

    elif isinstance(new, Variable):
        new: Variable
        old: Variable
        node_change = VariableChange(MODIFIED, new, old)
        node_change.type = (new.type != old.type)
        updated = node_change.type

    elif isinstance(new, Enum):
        new: Enum
        old: Enum
        node_change = EnumChange(MODIFIED, new, old)
        deleted, matched, added = match_items(new.values, old.values)
        for value in deleted:
            node_change.values.append(EnumValueChange(DELETED, value, value))
        for value in added:
            node_change.values.append(EnumValueChange(ADDED, value, value))
        for new_value, old_value in matched:
            if type(new_value) == str:
                new_value = new_value
            value_change = EnumValueChange(MODIFIED, new_value, old_value)
            value_change.index = (new_value.index != old_value.index)
            value_change.value = (new_value.value != old_value.value)
            value_change.desc = (new_value.desc != old_value.desc)
            if value_change.index or value_change.value or value_change.desc:
                node_change.values.append(value_change)
        updated = (len(node_change.values) != 0)

    elif isinstance(new, Struct):
        new: Struct
        old: Struct
        node_change = StructChange(MODIFIED, new, old)
        deleted, matched, added = match_items(new.fields, old.fields)
        for field in deleted:
            node_change.fields.append(StructFieldChange(DELETED, field, field))
        for field in added:
            node_change.fields.append(StructFieldChange(ADDED, field, field))
        for new_field, old_field in matched:
            field_change = StructFieldChange(MODIFIED, new_field, old_field)
            field_change.index = (new_field.index != old_field.index)
            field_change.type = (new_field.type != old_field.type)
            field_change.desc = (new_field.desc != old_field.desc)
            if field_change.index or field_change.type or field_change.desc:
                node_change.fields.append(field_change)
        updated = (len(node_change.fields) != 0)

    elif isinstance(new, FunctionLike):
        new: FunctionLike
        old: FunctionLike
        if isinstance(new, Function):
            node_change = FunctionChange(MODIFIED, new, old)
            node_change.return_type = (new.return_type != old.return_type)
            updated = node_change.return_type
        else:
            node_change = DefineChange(MODIFIED, new, old)
            node_change.value = (new.value != old.value)
            updated = node_change.value
        deleted, matched, added = match_items(new.params, old.params)
        for param in deleted:
            node_change.params.append(ParamChange(DELETED, param, param))
        for param in added:
            node_change.params.append(ParamChange(ADDED, param, param))
        for new_param, old_param in matched:
            param_change = ParamChange(MODIFIED, new_param, old_param)
            param_change.index = (new_param.index != old_param.index)
            param_change.type = (new_param.type != old_param.type)
            param_change.desc = (new_param.desc != old_param.desc)
            if param_change.index or param_change.type or param_change.desc:
                node_change.params.append(param_change)
        updated = updated or (len(node_change.params) != 0)
    else:
        raise ValueError(str(new))

    node_change.file = (new.file != old.file)
    node_change.desc = (new.desc != old.desc)

    if updated or node_change.file or node_change.desc:
        result.append(node_change)

    return result


def match_nodes(new: ParseResult, old: ParseResult):
    deleted: 'list[Node]' = []
    matched: 'list[tuple[Node, Node]]' = []
    added: 'list[Node]' = []
    old_matched: 'set[Node]' = set()

    for short_id, new_node in new.nodes_by_short_id.items():
        if short_id in old.nodes_by_short_id:
            old_node = old.nodes_by_short_id[short_id]
            if isinstance(new_node, dict) and isinstance(old_node, dict):
                match_groups(matched, added, old_matched, new_node, old_node)
            elif isinstance(new_node, dict):
                match_groups(matched, added, old_matched, new_node, { old_node.file: old_node })
            elif isinstance(old_node, dict):
                match_groups(matched, added, old_matched, { new_node.file: new_node }, old_node)
            else:
                matched.append((new_node, old_node))
                old_matched.add(old_node)
        else:
            if isinstance(new_node, dict):
                for n in new_node.values():
                    added.append(n)
            else:
                added.append(new_node)

    deleted = list(set(old.nodes) - old_matched)

    changes:'list[AnyChange]' = []

    for node in deleted:
        changes.append(get_add_delete(node, DELETED))

    for node in added:
        changes.append(get_add_delete(node, ADDED))

    for nodes in matched:
        changes.extend(get_changes(nodes[0], nodes[1]))

    dump_json(changes, '../changes.json')

    print(len(deleted), len(matched), len(added))
    tmp = ParseResult()
    tmp.nodes = deleted
    dump_doxygen_json(tmp, Path('../del.json'))


def dump_json(data: Any, file: Path):
    def default_nodes(o):
        if isinstance(o, set):
            return list(o)
        else:
            d = {'__id__': id(o)}
            for name in tuple(dir(o)):
                if not name.startswith('_'):
                    value = getattr(o, name)
                    if not callable(value):
                        d[name] = value
            return d
    nodes_json = JSONEncoder(sort_keys=False, indent=4, default=default_nodes).encode(data)
    with open(file, 'w') as fd:
        fd.write(nodes_json)


new = parse_doxygen(Path('../new.pkl'))
old = parse_doxygen(Path('../old.pkl'))

#dump_doxygen_json(new, '../new.json')
#dump_doxygen_json(new, '../old.json')

match_nodes(new, old)
