
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
from compare import AnyChange, compare_nodes
#from outputs import generate_output

HEADER_FILE_EXTENSION = '.h'

XML_DIR = Path('zephyr/doc/_build/doxygen/xml')

#res = parse_doxygen(XML_DIR)
#res = parse_doxygen(Path('../a.pkl'))
#save_doxygen(res, '../new.pkl'); dump_doxygen_json(res, '../new.json')
#save_doxygen(res, '../old.pkl'); dump_doxygen_json(res, '../old.json')


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

changes = compare_nodes(new, old)
dump_json(changes, '../changes.json')


from jinja2 import Template, filters

# cached_templates = {}

# data_prototype = {
#     '_notice_': '\n<<-------------------------NOTICE-------------------------->>\n',
#     '_warning_': '\n<<-------------------------WARNING-------------------------->>\n',
#     '_error_': '\n<<-------------------------ERROR-------------------------->>\n',
#     '_details_': '\n<<-------------------------DETAILS-------------------------->>\n',
# }

# for change in changes:
#     template_id = f'{change.kind}-{change.action}'
#     if template_id in cached_templates:
#         template = cached_templates[template_id]
#     else:
#         template_file = Path(__file__).parent / 'templates' / (template_id + '.md.jinja2')
#         if not template_file.exists():
#             print(f'Template {template_file} is missing.')
#             continue
#         template_source = template_file.read_text()
#         template = Template(template_source)
#         cached_templates[template_id] = template
#     data = dict(data_prototype)
#     for name in dir(change):
#         value = getattr(change, name)
#         if (not callable(value)) and (not name.startswith('_')):
#             data[name] = value
#     output = template.render(**data).strip()
#     print(output)


def generate_output(kind: str | None, change: AnyChange, **kwargs) -> str:
    template_name = f'{kind or change.kind}_{change.action}.md.jinja2'
    if template_name in cached_templates:
        template = cached_templates[template_name]
    else:
        template_file = Path(__file__).parent / 'templates' / template_name
        if not template_file.exists():
            print(f'Template {template_file} is missing.')
            return ''
        template_source = template_file.read_text()
        template = Template(template_source)
        cached_templates[template_name] = template
    data = dict(data_prototype)
    for name in dir(change):
        value = getattr(change, name)
        if (not callable(value)) and (not name.startswith('_')):
            data[name] = value
    for name, value in kwargs.items():
        data[name] = value
    data['this'] = change
    return template.render(**data)


cached_templates = {}


data_prototype = {
    '_notice_': '\n<<-------------------------NOTICE-------------------------->>\n',
    '_warning_': '\n<<-------------------------WARNING-------------------------->>\n',
    '_error_': '\n<<-------------------------ERROR-------------------------->>\n',
    '_details_': '\n<<-------------------------DETAILS-------------------------->>\n',
    '_generate_': generate_output
}

for change in changes:
    print(generate_output(None, change))
