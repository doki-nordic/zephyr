

from compare import AnyChange, CompareResult
from jinja2 import Template


def compile_messages(messages):
    result = {}
    for key in list(messages.keys()):
        message = messages[key]
        level = 0
        if message.startswith('notice:'):
            level = 1
        elif message.startswith('warning:'):
            level = 2
        elif message.startswith('critical:'):
            level = 3
        result[key] = (Template(messages[key]), level)
    return result


messages: 'dict[str, tuple[Template, int]]' = compile_messages({
    'typedef-added': 'notice: New type "{{new.name}}" definition added.',
    'typedef-deleted': 'critical: Type "{{old.name}}" definition deleted.',
    'typedef-modified-file': 'warning: Type "{{new.name}}" definition moved to a different file.',
    'typedef-modified-desc': 'notice: Type "{{new.name}}" definition description changed.',
    'typedef-modified-type': 'warning: Type "{{new.name}}" definition changed.',
    'var-added': 'notice: New variable "{{new.name}}" added.',
    'var-deleted': 'critical: Variable "{{old.name}}" deleted.',
    'var-modified-file': 'warning: Variable "{{new.name}}" moved to a different file.',
    'var-modified-desc': 'notice: Variable "{{new.name}}" description changed.',
    'var-modified-type': 'warning: Variable "{{new.name}}" type changed.',
    'enum_value-added': 'notice: New enum value "{{new.name}}" added.',
    'enum_value-deleted': 'critical: Enum value "{{old.name}}" deleted.',
    'enum_value-modified-value': 'warning: Enum value "{{new.name}}" changed.',
    'enum_value-modified-desc': 'notice: Enum value "{{new.name}}" description changed.',
    'enum_value-modified-file': 'warning: Enum value "{{new.name}}" moved to a different file.',
    'enum-added': 'notice: New enum "{{new.name}}" added.',
    'enum-deleted': 'critical: Enum "{{old.name}}" deleted.',
    'enum-modified-file': 'warning: Enum "{{new.name}}" moved to a different file.',
    'enum-modified-desc': 'notice: Enum "{{new.name}}" description changed.',
    'struct-added': 'notice: New structure "{{new.name}}" added.',
    'struct-deleted': 'critical: Structure "{{old.name}}" deleted.',
    'struct-modified-file': 'warning: Structure "{{new.name}}" moved to a different file.',
    'struct-modified-desc': 'notice: Structure "{{new.name}}" description changed.',
    'func-added': 'notice: Function "{{new.name}}" added.',
    'func-deleted': 'critical: Function "{{old.name}}" deleted.',
    'func-modified-return_type': 'warning: Function "{{new.name}}" return type changed.',
    'func-modified-file': 'warning: Function "{{new.name}}" moved to a different file.',
    'func-modified-desc': 'notice: Function "{{new.name}}" description changed.',
    'def-added': 'notice: Definition "{{new.name}}" added.',
    'def-deleted': 'critical: Definition "{{old.name}}" deleted.',
    'def-modified-value': 'notice: Definition "{{new.name}}" value changed.',
    'def-modified-file': 'warning: Definition "{{new.name}}" moved to a different file.',
    'def-modified-desc': 'notice: Definition "{{new.name}}" description changed.',
    'field-added': 'notice: Structure "{{struct.new.name}}" field "{{new.name}}" added.',
    'field-deleted': 'critical: Structure "{{struct.new.name}}" field "{{new.name}}" deleted.',
    'field-modified-index': 'notice: Structure "{{struct.new.name}}" field "{{new.name}}" reordered.',
    'field-modified-type': 'warning: Structure "{{struct.new.name}}" field "{{new.name}}" type changed.',
    'field-modified-desc': 'notice: Structure "{{struct.new.name}}" field "{{new.name}}" description changed.',
    'param-added': 'critical: Parameter "{{new.name}}" added in "{{parent.new.name}}".',
    'param-deleted': 'critical: Parameter "{{old.name}}" deleted from "{{parent.new.name}}".',
    'param-modified-index': 'critical: Parameter "{{new.name}}" reordered in "{{parent.new.name}}".',
    'param-modified-type': 'warning: Parameter "{{new.name}}" type changed in "{{parent.new.name}}".',
    'param-modified-desc': 'notice: Parameter "{{new.name}}" description changed in "{{parent.new.name}}".',
})


def generate_changes(changes: 'list[AnyChange]', location: str, **kwargs) -> int:
    max_level = 0
    for change in changes:
        prefix = f'{change.kind}-{change.action}'
        if change.new and hasattr(change.new, 'file') and change.new.file:
            if change.new and hasattr(change.new, 'line') and change.new.line:
                loc = f'{change.new.file}:{change.new.line}:'
            else:
                loc = f'{change.new.file}:'
        else:
            loc = location
        for key, (template, level) in messages.items():
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
                    max_level = max(level, max_level)
                else:
                    field = key[len(prefix) + 1:]
                    value = getattr(change, field)
                    if (value):
                        print(loc, message)
                        max_level = max(level, max_level)
        if prefix == 'struct-modified':
            level = generate_changes(change.fields, loc, struct=change)
            max_level = max(level, max_level)
        elif prefix in ('func-modified', 'def-modified'):
            level = generate_changes(change.params, loc, parent=change)
            max_level = max(level, max_level)
    return max_level


def generate(compare_result: CompareResult):
    max_level = 0
    for group in compare_result.groups:
        if (group.name):
            print(f'=== Group {group.name}: {group.title} ===')
        level = generate_changes(group.changes, '')
        max_level = max(level, max_level)
    return max_level
