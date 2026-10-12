"""Inventory GH2's function and variable dependencies from its XEX import table."""
import json
import re
import struct
from pathlib import Path
xex = Path('private/default.xex').read_bytes()
image = Path('private/default.exe').read_bytes()
count = struct.unpack_from('>I', xex, 20)[0]
headers = dict(struct.unpack_from('>II', xex, 24 + i * 8) for i in range(count))
offset = headers[0x103FF]
size, string_size, module_count = struct.unpack_from('>3I', xex, offset)
names = [s.decode('ascii') for s in xex[offset + 12:offset + 12 + string_size].split(b'\0') if s]
cursor = offset + 12 + string_size
imports = []
for module_index in range(module_count):
    name = names[module_index]
    table = Path('tools/XenonRecomp/XenonUtils/xbox') / ('xam_table.inc' if name == 'xam.xex' else 'xboxkrnl_table.inc')
    exports = {int(ordinal, 16): (symbol, kind) for ordinal, symbol, kind in re.findall(r'XE_EXPORT\(\w+,\s*(0x[0-9A-Fa-f]+),\s*(\w+),\s*(\w+)\)', table.read_text())}
    record_size = struct.unpack_from('>I', xex, cursor)[0]
    record_count = struct.unpack_from('>H', xex, cursor + 38)[0]
    for i in range(record_count):
        address = struct.unpack_from('>I', xex, cursor + 40 + i * 4)[0]
        word = struct.unpack_from('>I', image, address - 0x82000000)[0]
        ordinal = word & 0xFFFF
        symbol, kind = exports.get(ordinal, ('unknown', 'unknown'))
        imports.append({'module': name, 'name': symbol, 'kind': kind, 'ordinal': f'0x{ordinal:04X}', 'address': f'0x{address:08X}', 'record_type': word >> 24, 'unresolved_word': f'0x{word:08X}'})
    cursor += record_size
variables = [entry for entry in imports if entry['kind'] == 'kVariable']
functions = sorted({entry['name'] for entry in imports if entry['kind'] == 'kFunction'})
report = {'function_import_count': len(functions), 'variable_import_count': len(variables), 'variable_imports': variables, 'function_imports': functions}
Path('docs/import_inventory.json').write_text(json.dumps(report, indent=2))
print(json.dumps({k: v for k, v in report.items() if k != 'function_imports'}, indent=2))
