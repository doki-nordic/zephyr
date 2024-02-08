

from compare import AnyChange, CompareResult
from jinja2 import Template


def compile_messages(messages):
    result = {}
    for key in list(messages.keys()):
        result[key] = Template(messages[key])
    return result


messages: 'dict[str, Template]' = compile_messages({
    'typedef-added': 'notice: New type "{{new.name}}" definition added.',
    'typedef-deleted': 'error: Type "{{old.name}}" definition deleted.',
    'typedef-modified-file': 'warning: Type "{{new.name}}" definition moved to a different file.',
    'typedef-modified-desc': 'notice: Type "{{new.name}}" definition description changed.',
    'typedef-modified-type': 'warning: Type "{{new.name}}" definition changed.',
    'var-added': 'notice: New variable "{{new.name}}" added.',
    'var-deleted': 'error: Variable "{{old.name}}" deleted.',
    'var-modified-file': 'warning: Variable "{{new.name}}" moved to a different file.',
    'var-modified-desc': 'notice: Variable "{{new.name}}" description changed.',
    'var-modified-type': 'warning: Variable "{{new.name}}" type changed.',
    'enum-added': 'notice: New enum "{{new.name}}" added.',
    'enum-deleted': 'error: Enum "{{old.name}}" deleted.',
    'enum-modified-file': 'warning: Enum "{{new.name}}" moved to a different file.',
    'enum-modified-desc': 'notice: Enum "{{new.name}}" description changed.',
    'struct-added': 'notice: New structure "{{new.name}}" added.',
    'struct-deleted': 'error: Structure "{{old.name}}" deleted.',
    'struct-modified-file': 'warning: Structure "{{new.name}}" moved to a different file.',
    'struct-modified-desc': 'notice: Structure "{{new.name}}" description changed.',
    'func-added': 'notice: Function "{{new.name}}" added.',
    'func-deleted': 'error: Function "{{old.name}}" deleted.',
    'func-modified-return_type': 'warning: Function "{{new.name}}" return type changed.',
    'func-modified-file': 'warning: Function "{{new.name}}" moved to a different file.',
    'func-modified-desc': 'notice: Function "{{new.name}}" description changed.',
    'def-added': 'notice: Definition "{{new.name}}" added.',
    'def-deleted': 'error: Definition "{{old.name}}" deleted.',
    'def-modified-value': 'notice: Definition "{{new.name}}" value changed.',
    'def-modified-file': 'warning: Definition "{{new.name}}" moved to a different file.',
    'def-modified-desc': 'notice: Definition "{{new.name}}" description changed.',
    'enum_value-added': 'notice: New enum "{{enum.new.name}}" value "{{new.name}}" added.',
    'enum_value-deleted': 'error: Enum "{{enum.new.name}}" value "{{old.name}}" deleted.',
    'enum_value-modified-index': 'warning: Enum "{{enum.new.name}}" value "{{new.name}}" reordered.',
    'enum_value-modified-value': 'warning: Enum "{{enum.new.name}}" value "{{new.name}}" changed.',
    'enum_value-modified-desc': 'notice: Enum "{{enum.new.name}}" value "{{new.name}}" description changed.',
    'field-added': 'notice: Structure "{{struct.new.name}}" field "{{new.name}}" added.',
    'field-deleted': 'error: Structure "{{struct.new.name}}" field "{{new.name}}" deleted.',
    'field-modified-index': 'notice: Structure "{{struct.new.name}}" field "{{new.name}}" reordered.',
    'field-modified-type': 'warning: Structure "{{struct.new.name}}" field "{{new.name}}" type changed.',
    'field-modified-desc': 'notice: Structure "{{struct.new.name}}" field "{{new.name}}" description changed.',
    'param-added': 'error: Parameter "{{new.name}}" added in "{{parent.new.name}}".',
    'param-deleted': 'error: Parameter "{{old.name}}" deleted in "{{parent.new.name}}".',
    'param-modified-index': 'error: Parameter "{{new.name}}" reordered in "{{parent.new.name}}".',
    'param-modified-type': 'warning: Parameter "{{new.name}}" type changed in "{{parent.new.name}}".',
    'param-modified-desc': 'notice: Parameter "{{new.name}}" description changed in "{{parent.new.name}}".',
})


def generate_changes(changes: 'list[AnyChange]', location: str, **kwargs):
    for change in changes:
        prefix = f'{change.kind}-{change.action}'
        if change.new and hasattr(change.new, 'file') and change.new.file:
            if change.new and hasattr(change.new, 'line') and change.new.line:
                loc = f'{change.new.file}:{change.new.line}:'
            else:
                loc = f'{change.new.file}:'
        else:
            loc = location
        for key, template in messages.items():
            if key.startswith(prefix):
                data = {}
                for name in dir(change):
                    value = getattr(change, name)
                    if (not callable(value)) and (not name.startswith('_')):
                        data[name] = value
                for name, value in kwargs.items():
                    data[name] = value
                message = template.render(**data)
                if key == prefix:
                    print(loc, message)
                else:
                    field = key[len(prefix) + 1:]
                    value = getattr(change, field)
                    if (value):
                        print(loc, message)
        if prefix == 'enum-modified':
            generate_changes(change.values, loc, enum=change)
        elif prefix == 'struct-modified':
            generate_changes(change.fields, loc, struct=change)
        elif prefix in ('func-modified', 'def-modified'):
            generate_changes(change.params, loc, parent=change)

def generate(compare_result: CompareResult):
    for group in compare_result.groups:
        if (group.name):
            print(f'=== Group {group.name}: {group.title} ===')
        generate_changes(group.changes, '')