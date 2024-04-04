
from pathlib import Path
import sys
import json
import argparse


class ArgsClass:
    new_input: Path
    old_input: 'Path | None'
    format: str
    save_input: 'Path | None'
    save_old_input: 'Path | None'
    dump_json: 'Path | None'


def parse_args() -> ArgsClass:
    parser = argparse.ArgumentParser(add_help=False,
                                     description='Detect API changes based on doxygen XML output.')
    parser.add_argument('new_input', metavar='new-input', type=Path,
                        help='The directory containing doxygen XML output or pre-parsed file with ' +
                             'the new API version. For details about ' +
                             'doxygen XML output, see https://www.doxygen.nl/manual/output.html.')
    parser.add_argument('old_input', metavar='old-input', nargs='?', type=Path,
                        help='The directory containing doxygen XML output or pre-parsed file with ' +
                             'the old API version. You should skip this if you want to pre-parse ' +
                             'the input with the "--save-input" option.')
    parser.add_argument('--format', choices=('text', 'markdown', 'json'), default='text',
                        help='Output format. Default is "text".')
    parser.add_argument('--save-input', metavar='FILE', type=Path,
                        help='Pre-parse and save the "new-input" to a file. The file format may change ' +
                             'from version to version. Use always the same version ' +
                             'of this tool for one file.')
    parser.add_argument('--save-old-input', metavar='FILE', type=Path,
                        help='Pre-parse and save the "old-input" to a file.')
    parser.add_argument('--dump-json', metavar='FILE', type=Path,
                        help='Dump input data to a JSON file (only for debug purposes).')
    parser.add_argument('--help', action='help',
                        help='Show this help and exit.')
    args: ArgsClass = parser.parse_args()

    if (args.old_input is None) and (args.save_input is None):
        parser.print_usage()
        print('error: at least one of the following arguments is required: old-input, --save-input')
        sys.exit(2)

    return args


def parse_args2() -> ArgsClass:
    a = ArgsClass()
    a.dump_json = Path('/home/doki/work/zephyr/dump.json')
    a.format = 'text'
    a.new_input = Path('/home/doki/work/zephyr/new.pkl')
    a.old_input = Path('/home/doki/work/zephyr/old.pkl')
    #a.new_input = Path('/home/doki/work/zephyr/zephyr/doc/_build/doxygen/xml')
    #a.old_input = Path('/home/doki/work/zephyr/zephyr/doc/_build/doxygen/xml')
    a.save_input = None
    a.save_old_input = None
    return a

args: ArgsClass = parse_args()


if __name__ == '__main__':
    print(json.dumps(args.__dict__, indent=4, default=lambda x: str(x)))
