


from pathlib import Path
from jinja2 import Template, filters

from compare import AnyChange


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


# from compare import ADDED, DELETED, DefineChange, FunctionChange, ParamChange

# _notice_ = '\n<<-------------------------NOTICE-------------------------->>\n'
# _warning_ = '\n<<-------------------------WARNING-------------------------->>\n'
# _error_ = '\n<<-------------------------ERROR-------------------------->>\n'
# _details_ = '\n<<-------------------------DETAILS-------------------------->>\n'


# def generate_output(p, kind: str, action: str, *args):
#     name = f'{kind}_{action}'
#     all = globals()
#     if name in all:
#         func = all[name]
#         func(p, *args)
#     else:
#         print(f'TODO:', name)


# def def_added(p, change: DefineChange):
#     p(f'''{_notice_}
#         Definition `{change.new.name}` was added in `{change.new.file}:{change.new.line}`.

#         If, starting from this version, a user is obligated to use it,
#         add such information to the migration guide.
#         ''')

# def def_param_added(p, change: DefineChange, param_change: ParamChange):
#     p(f'''
#     { _error_ }
#     New macro parameter `{ param_change.new.name }` was added in `{change.new.name}`
#     in `{change.new.file}:{change.new.line}`.
#     This breaks API compatibility.
#     ''')

# def def_param_deleted(p, change: DefineChange, param_change: ParamChange):
#     p(f'''
#     { _error_ }
#     New macro parameter `{ param_change.new.name }` was added in `{param_change.new.name}`
#     in `{change.new.file}:{change.new.line}`.
#     This breaks API compatibility.
#     ''')

# def def_param_modified(p, change: DefineChange, param_change: ParamChange):
#     if param_change.index:
#         p(f'''
#         { _error_ }
#         A macro parameter `{ param_change.new.name }` was reordered in `{change.new.name}` in `{change.new.file}:{change.new.line}`.
#         This breaks API compatibility.
#         ''')
#     if param_change.desc:
#         p(f'''
#         {{ _notice_ }}
#         A macro parameter `{ param_change.new.name }` description was changed
#         in `{change.new.name}` in `{change.new.file}:{change.new.line}`.

#         If this is a behavioral change that affects API compatibility, add it to the migration guide.
#         ''')


# def def_modified(p, change: DefineChange):
#     for param_change in change.params:
#         generate_output(p, 'def_param', param_change.action, change, param_change)
#     if change.value:
#         p(f'''
#         { _notice_ }
#         Value of the definition `{change.new.name}` was changed in `{change.new.file}:{change.new.line}`.

#         If the value is not opaque to the user, follow the migration procedure and
#         provide details in the migration guide.
#         ''')
#     if change.desc:
#         p(f'''
#         { _notice_ }
#         A definition `{ change.new.name }` description was changed in `{change.new.file}:{change.new.line}`.

#         If this is a behavioral change that affects API compatibility, add it to the migration guide.
#         ''')
#     if change.file:
#         p(f'''
#         { _warning_ }
#         A definition `{ change.new.name }` was moved from `{change.old.file}:{change.old.line}`
#         to `{change.new.file}:{change.new.line}`.

#         If this changes includes in user code, follow the migration procedure and add it
#         to the migration guide.
#         ''')

# def func_modified(p, change: FunctionChange):
#     for param_change in change.params:
#         generate_output(p, 'func_param', param_change.action, change, param_change)
#     if change.return_type:
#         p(f'''
#         { _error_ }
#         Return type of the function `{change.new.name}` was changed
#         in `{change.new.file}:{change.new.line}`.

#         If user need to handle the returned value in any different way ollow the migration
#         procedure and add it to the migration guide.
#         ''')
#     if change.desc:
#         p(f'''
#         { _notice_ }
#         A definition `{ change.new.name }` description was changed in `{change.new.file}:{change.new.line}`.

#         If this is a behavioral change that affects API compatibility, add it to the migration guide.
#         ''')
#     if change.file:
#         p(f'''
#         { _warning_ }
#         A definition `{ change.new.name }` was moved from `{change.old.file}:{change.old.line}`
#         to `{change.new.file}:{change.new.line}`.

#         If this changes includes in user code, follow the migration procedure and add it
#         to the migration guide.
#         ''')
