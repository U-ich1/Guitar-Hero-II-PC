"""Read an XDVDFS disc without mounting or modifying it."""
import argparse
import json
import struct
from pathlib import Path

SECTOR = 2048
MAGIC = b'MICROSOFT*XBOX*MEDIA'

def inspect(iso, output, extract_all=False):
    with iso.open('rb') as disc:
        base = None
        for candidate in (0, 0xFD90000, 0x19590000):
            disc.seek(candidate + 32 * SECTOR)
            descriptor = disc.read(SECTOR)
            if descriptor[:20] == MAGIC and descriptor[-20:] == MAGIC:
                base = candidate
                break
        if base is None:
            raise ValueError('No recognized XDVDFS volume descriptor')
        root_sector, root_size = struct.unpack_from('<II', descriptor, 20)
        entries = []
        visited = set()

        def directory(sector, size, parent):
            key = (sector, size)
            if key in visited:
                raise ValueError('Repeated directory allocation')
            visited.add(key)
            disc.seek(base + sector * SECTOR)
            table = disc.read(size)
            seen = set()

            def node(offset):
                if offset in seen or offset + 14 > len(table):
                    raise ValueError('Invalid directory node')
                seen.add(offset)
                left, right, start, length, attrs, name_size = struct.unpack_from('<HHIIBB', table, offset)
                if offset + 14 + name_size > len(table):
                    raise ValueError('Truncated name')
                name = table[offset + 14:offset + 14 + name_size].decode('ascii')
                if not name or name in ('.', '..') or any(c in name for c in '/\\:'):
                    raise ValueError('Unsafe filename')
                relative = parent / name
                absolute = base + start * SECTOR
                if absolute + length > iso.stat().st_size:
                    raise ValueError('Allocation outside disc')
                if left:
                    node(left * 4)
                entries.append({'path': relative.as_posix(), 'sector': start, 'size': length, 'directory': bool(attrs & 16)})
                if attrs & 16 and length:
                    directory(start, length, relative)
                if right:
                    node(right * 4)
            if size:
                node(0)

        directory(root_sector, root_size, Path())
        output.mkdir(parents=True, exist_ok=True)
        report = {'iso': iso.name, 'partition_offset': base, 'entries': entries}
        (output / 'disc_manifest.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        executables = [e for e in entries if e['path'].lower().endswith('.xex') and not e['directory']]
        selected = [e for e in entries if not e['directory'] and not e['path'].startswith('$SystemUpdate/')] if extract_all else executables
        for entry in selected:
            target = output / entry['path']
            target.parent.mkdir(parents=True, exist_ok=True)
            disc.seek(base + entry['sector'] * SECTOR)
            remaining = entry['size']
            with target.open('wb') as destination:
                while remaining:
                    data = disc.read(min(remaining, 8 * 1024 * 1024))
                    if not data:
                        raise ValueError('Truncated disc file')
                    destination.write(data)
                    remaining -= len(data)
        print(json.dumps({'partition_offset': base, 'entry_count': len(entries), 'executables': executables}, indent=2))

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('iso', type=Path)
    parser.add_argument('--output', type=Path, default=Path('private'))
    parser.add_argument('--all', action='store_true', help='Extract game data too (skip console system update)')
    args = parser.parse_args()
    inspect(args.iso, args.output, args.all)
