

import sys
from typing import Any
from compare import AnyChange, CompareResult
import json

def object_convert(o: Any):
        if isinstance(o, set):
            return list(o)
        else:
            d = {}
            for name in tuple(dir(o)):
                if not name.startswith('_'):
                    value = getattr(o, name)
                    if not callable(value):
                        d[name] = value
            return d

def generate(compare_result: CompareResult):
    json.dump(compare_result, fp=sys.stdout, default=object_convert, indent=4)
    return 1 if len(compare_result.changes) else 0
