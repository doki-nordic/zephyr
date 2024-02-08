

from compare import AnyChange, CompareResult

messages = {
    'typedef-added': 'notice: New type definition added.',
    'typedef-deleted': 'error: Type definition deleted.',
    'typedef-modified-file': 'warning: Type definition moved to a different file.',
    'typedef-modified-desc': 'notice: Type definition description changed.',
    'typedef-modified-type': 'warning: Type definition changed.',
    'var-added': 'notice: New variable added.',
    'var-deleted': 'error: Variable deleted.',
    'var-modified-file': 'warning: Variable moved to a different file.',
    'var-modified-desc': 'notice: Variable description changed.',
    'var-modified-type': 'warning: Variable type changed.',
    'enum-added': 'notice: New enum added.',
    'enum-deleted': 'error: Enum deleted.',
    'enum-modified-file': 'warning: Enum moved to a different file.',
    'enum-modified-desc': 'notice: Enum description changed.',
    'struct-added': 'notice: New structure added.',
    'struct-deleted': 'error: Structure deleted.',
    'struct-modified-file': 'warning: Structure moved to a different file.',
    'struct-modified-desc': 'notice: Structure description changed.',
    'func-added': 'notice: Function added.',
    'func-deleted': 'error: Function deleted.',
    'func-modified-return_type': 'warning: Function return type changed.',
    'func-modified-file': 'warning: Function moved to a different file.',
    'func-modified-desc': 'notice: Function description changed.',
    'def-added': 'notice: Definition added.',
    'def-deleted': 'error: Definition deleted.',
    'def-modified-value': 'notice: Definition value changed.',
    'def-modified-file': 'warning: Definition moved to a different file.',
    'def-modified-desc': 'notice: Definition description changed.',
    'enum_value-added': 'notice: New enum value added.',
    'enum_value-deleted': 'error: Enum value deleted.',
    'enum_value-modified-index': 'warning: Enum value reordered.',
    'enum_value-modified-value': 'warning: Enum value changed.',
    'enum_value-modified-desc': 'notice: Enum value description changed.',
    'field-added': 'notice: Structure field added.',
    'field-deleted': 'error: Structure field deleted.',
    'field-modified-index': 'notice: Structure field reordered.',
    'field-modified-type': 'warning: Structure field type changed.',
    'field-modified-desc': 'notice: Structure field description changed.',
    'param-added': 'error: Parameter added.',
    'param-deleted': 'error: Parameter deleted.',
    'param-modified-index': 'error: Parameter reordered.',
    'param-modified-type': 'warning: Parameter type changed.',
    'param-modified-desc': 'notice: Parameter description changed.',
}

def generate_changes(changes: 'list[AnyChange]', loc: 'str | None'):
    for change in changes:
        prefix = f'{change.kind}-{change.action}'
        if not loc:
            if change.new.line:
                loc = f'{change.new.file}:{change.new.line}:'
            else:
                loc = f'{change.new.file}:'
        for key, message in messages.items():
            if key == prefix:
                print(loc, message)
            elif key.startswith(prefix):
                field = key[len(prefix) + 1:]
                value = getattr(change, field)
                if (value):
                    print(loc, message)
        if prefix == 'enum-modified':
            generate_changes(change.values, loc)
        elif prefix == 'struct-modified':
            generate_changes(change.fields, loc)
        elif prefix in ('func-modified', 'def-modified'):
            generate_changes(change.params, loc)

def generate(compare_result: CompareResult):
    for group in compare_result.groups:
        if (group.name):
            print(f'=== Group {group.name}: {group.title}')
            generate_changes(group.changes, None)