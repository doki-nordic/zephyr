
'''
pip install doxmlparser
cd zephyr/doc
make configure
cd _build
ninja doxygen
'''

from json import JSONEncoder
from pathlib import Path
import pickle
from dox_parser import dump_doxygen_json, parse_doxygen, save_doxygen


HEADER_FILE_EXTENSION = '.h'

XML_DIR = Path('zephyr/doc/_build/doxygen/xml')

res = parse_doxygen(XML_DIR)
#res = parse_doxygen(Path('../a.pkl'))
save_doxygen(res, '../new.pkl'); dump_doxygen_json(res, '../new.json')
#save_doxygen(res, '../old.pkl'); dump_doxygen_json(res, '../old.json')
