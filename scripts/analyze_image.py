"""Decode this disc's basic-compressed XEX and inspect its PE sections."""
import hashlib
import json
import struct
from pathlib import Path
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

data = Path('private/default.xex').read_bytes()
u32 = lambda offset: struct.unpack_from('>I', data, offset)[0]
headers = dict(struct.unpack_from('>II', data, 24 + i * 8) for i in range(u32(20)))
security, payload = u32(16), u32(8)
fmt = headers[0x3FF]
size, encryption, compression = struct.unpack_from('>IHH', data, fmt)
if (encryption, compression) != (1, 1):
    raise ValueError('This inspector supports retail encrypted basic compression only')
def decrypt(key, content):
    context = Cipher(algorithms.AES(key), modes.CBC(bytes(16))).decryptor()
    return context.update(content) + context.finalize()
retail_key = bytes.fromhex('20B185A59D28FDC340583FBB0896BF91')
session = decrypt(retail_key, data[security + 0x150:security + 0x160])
plain = decrypt(session, data[payload:])
image = bytearray()
cursor = 0
for offset in range(fmt + 8, fmt + size, 8):
    stored, zeros = struct.unpack_from('>II', data, offset)
    image.extend(plain[cursor:cursor + stored])
    image.extend(bytes(zeros))
    cursor += stored
if image[:2] != b'MZ':
    raise ValueError('Decoded payload has no DOS header')
pe = struct.unpack_from('<I', image, 0x3C)[0]
if image[pe:pe + 4] != b'PE\0\0':
    raise ValueError('Decoded payload has no PE signature')
machine, count = struct.unpack_from('<HH', image, pe + 4)
optional_size = struct.unpack_from('<H', image, pe + 20)[0]
base = headers[0x10201]
sections = []
patterns = {
    'restgprlr_14_address': 'E9C1FF68', 'savegprlr_14_address': 'F9C1FF68',
    'restfpr_14_address': 'C9CCFF70', 'savefpr_14_address': 'D9CCFF70',
    'restvmx_14_address': '3960FEE07DCB60CE', 'savevmx_14_address': '3960FEE07DCB61CE',
    'restvmx_64_address': '3960FC00100B60CB', 'savevmx_64_address': '3960FC00100B61CB',
}
candidates = {name: [] for name in patterns}
for index in range(count):
    offset = pe + 24 + optional_size + index * 40
    name = bytes(image[offset:offset + 8]).rstrip(b'\0').decode('ascii')
    virtual_size, rva, raw_size, raw_offset = struct.unpack_from('<4I', image, offset + 8)
    flags = struct.unpack_from('<I', image, offset + 36)[0]
    sections.append({'name': name, 'address': f'0x{base + rva:08X}', 'virtual_size': virtual_size,
                     'raw_size': raw_size, 'raw_offset': raw_offset, 'image_offset': rva, 'flags': f'0x{flags:08X}'})
    if flags & 0x20:
        # XEX payloads are memory images, not raw PE files. Section bytes are at RVA.
        code = image[rva:rva + virtual_size]
        for label, pattern in patterns.items():
            pattern = bytes.fromhex(pattern)
            for position in range(0, len(code) - len(pattern) + 1, 4):
                if code[position:position + len(pattern)] == pattern:
                    candidates[label].append(f'0x{base + rva + position:08X}')
Path('private/default.exe').write_bytes(image)
report = {'machine': f'0x{machine:04X}', 'decoded_size': len(image),
          'decoded_sha256': hashlib.sha256(image).hexdigest(), 'sections': sections,
          'register_helper_candidates': candidates}
Path('private/image_report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
