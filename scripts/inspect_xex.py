"""Report XEX header metadata; does not decrypt or execute game code."""
import hashlib
import json
import struct
from pathlib import Path

path = Path('private/default.xex')
data = path.read_bytes()
if data[:4] != b'XEX2':
    raise ValueError('Expected XEX2 executable')
flags, payload, reserved, security, count = struct.unpack_from('>5I', data, 4)
headers = dict(struct.unpack_from('>II', data, 24 + i * 8) for i in range(count))
report = {'file': str(path), 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
          'module_flags': f'0x{flags:08X}', 'payload_offset': payload}
for label, key in [('entry_point', 0x10100), ('image_base', 0x10201), ('stack_size', 0x20200)]:
    if key in headers:
        report[label] = f'0x{headers[key]:08X}'
if 0x3FF in headers:
    offset = headers[0x3FF]
    size, encryption, compression = struct.unpack_from('>IHH', data, offset)
    report.update(encryption_type=encryption, compression_type=compression)
if 0x40006 in headers:
    offset = headers[0x40006]
    media, version, base_version, title = struct.unpack_from('>4I', data, offset)
    report.update(title_id=f'{title:08X}', media_id=f'{media:08X}',
                  version=f'0x{version:08X}', base_version=f'0x{base_version:08X}')
if 0x103FF in headers:
    offset = headers[0x103FF]
    total, strings_size, libraries = struct.unpack_from('>3I', data, offset)
    strings = data[offset + 12:offset + 12 + strings_size]
    report['import_library_names'] = [s.decode('ascii') for s in strings.split(b'\0') if s]
    report['import_library_count'] = libraries
Path('private/xex_report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
