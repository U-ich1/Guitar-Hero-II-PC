"""Validate helper bodies against the memory image and add GH2's missed switch."""
import json
import struct
from pathlib import Path

image = Path('private/default.exe').read_bytes()
report = json.loads(Path('private/image_report.json').read_text())
base = 0x82000000
def words(address, count):
    return struct.unpack_from(f'>{count}I', image, address - base)

helpers = {name: int(addresses[0], 16) for name, addresses in report['register_helper_candidates'].items() if len(addresses) == 1}
if len(helpers) != 8:
    raise ValueError('All eight helper starts must have unique signatures')
for kind, address in helpers.items():
    if 'gpr' in kind:
        restore = kind.startswith('rest')
        op = 0xE8000000 if restore else 0xF8000000
        expected = [op | (reg << 21) | (1 << 16) | ((-152 + (reg - 14) * 8) & 0xFFFF) for reg in range(14, 32)]
        expected += [0x8181FFF8, 0x7D8803A6, 0x4E800020] if restore else [0x9181FFF8, 0x4E800020]
    elif 'fpr' in kind:
        op = 0xC8000000 if kind.startswith('rest') else 0xD8000000
        expected = [op | (reg << 21) | (12 << 16) | ((-144 + (reg - 14) * 8) & 0xFFFF) for reg in range(14, 32)] + [0x4E800020]
    else:
        start = 64 if '_64_' in kind else 14
        end = 128 if start == 64 else 32
        restore = kind.startswith('rest')
        expected = []
        for reg in range(start, end):
            expected.append(0x39600000 | ((-0x400 + (reg - 64) * 16) & 0xFFFF) if start == 64 else 0x39600000 | ((-0x120 + (reg - 14) * 16) & 0xFFFF))
            if start == 14:
                expected.append((0x7C0B60CE if restore else 0x7C0B61CE) | (reg << 21))
            else:
                # VMX128 register bits are split into low five and high bits.
                expected.append((0x100B60CB if restore else 0x100B61CB) | ((reg & 31) << 21) | ((reg & 32) >> 3))
        expected.append(0x4E800020)
    actual = words(address, len(expected))
    if list(actual) != expected:
        mismatch = next(i for i, (a, e) in enumerate(zip(actual, expected)) if a != e)
        raise ValueError(f'{kind} body mismatch at {address + mismatch * 4:08X}: {actual[mismatch]:08X} != {expected[mismatch]:08X}')

config = Path('config/gh2.toml').read_text()
import re
for name, address in helpers.items():
    config = re.sub(rf'^{name} = 0x[0-9A-F]+$', f'{name} = 0x{address:08X}', config, flags=re.M)
config = config.replace('switch_table_file_path = "gh2_switch_tables.toml"', 'switch_table_file_path = "gh2_all_switch_tables.toml"')
Path('config/gh2.toml').write_text(config)

# This 29-case switch has 50 instructions between its bounds check and table
# load. XenonAnalyse's fixed 32-instruction backwards search misses the check.
expected_dispatch = [0x3D80820C, 0x398C50F0, 0x5540083C, 0x7C0C022E, 0x3D808266, 0x398C54F8, 0x7D8C0214, 0x7D8903A6, 0x60000000, 0x4E800420]
if list(words(0x826654D0, len(expected_dispatch))) != expected_dispatch:
    raise ValueError('Missed-switch dispatch differs from this disc build')
if words(0x826653EC, 1)[0] != 0x2B0A001C or words(0x82665404, 1)[0] != 0x41991268:
    raise ValueError('Missed-switch bounds/default branch differ from this disc build')
offsets = struct.unpack_from('>29H', image, 0x820C50F0 - base)
labels = [0x826654F8 + offset for offset in offsets]
if any(address & 3 or not 0x82665370 <= address < 0x8266680C for address in labels):
    raise ValueError('Invalid missed-switch destination')
extra = '\n# GH2-specific switch: validated bound, table, and dispatch instructions.\n[[switch]]\nbase = 0x826654D0\nr = 10\ndefault = 0x8266666C\nlabels = [\n' + ''.join(f'    0x{address:08X},\n' for address in labels) + ']\n'
Path('config/gh2_all_switch_tables.toml').write_text(Path('config/gh2_switch_tables.toml').read_text() + extra)
print('Validated all eight complete register helper bodies and recovered the 29-case switch.')
