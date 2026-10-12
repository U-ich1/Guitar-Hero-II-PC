"""Build a small native-menu supplement from the user's supported GH2 dump.

ARK v3 layout reference: maxton/GameArchives (Library/Ark/ArkPackage.cs).
DTB node format reference: mtolly/dtab (src/Data/DTA/Base.hs).
No game content is distributed with this script; the original ARK is read-only.
"""
from pathlib import Path
import hashlib
import struct
import zlib

HDR_SHA256 = '2f414294916aa872c9efe5a4011b18aa71e7de7f974e18852d3ef4db9ba4491e'
MENU_SHA256 = 'bc81de7b6600458c21edf7b51dc4854ca650d41534bbd8df2c43ebce1c40b419'
MILO_SHA256 = '5788d67eac1df487e5a20f93d85a0be23760bfda8e3ba845e2949914af4bd904'

def patch_milo(source):
    if hashlib.sha256(source).hexdigest()!=MILO_SHA256:
        raise ValueError('Main-menu UI asset does not match the supported dump')
    magic,start,count,maximum=struct.unpack_from('<4I',source)
    if magic!=0xCBBEDEAF or not 0<count<128:raise ValueError('Unexpected milo container')
    pos=start;blocks=[]
    for index in range(count):
        size=struct.unpack_from('<I',source,16+4*index)[0]
        block=source[pos:pos+size];pos+=size
        blocks.append(zlib.decompress(block,-15))
    if pos!=len(source):raise ValueError('Milo block lengths changed')
    plain=b''.join(blocks)
    for before,after in ((b'leaderboards',b'OPTIONS'),(b'OPTIONS',b'pause_quit')):
        # Only change authored localized-text strings; component names stay intact.
        pattern=b'\x01'+struct.pack('<I',len(before))+before+b'\x01'
        if plain.count(pattern)!=1:raise ValueError('Menu label asset signature changed')
    # Replace OPTIONS first so the replacement of leaderboards is not changed again.
    for before,after in ((b'OPTIONS',b'pause_quit'),(b'leaderboards',b'OPTIONS')):
        pattern=b'\x01'+struct.pack('<I',len(before))+before+b'\x01'
        plain=plain.replace(pattern,b'\x01'+struct.pack('<I',len(after))+after+b'\x01',1)
    # One raw-deflate block preserves the serialized asset and its object delimiters.
    compressor=zlib.compressobj(9,zlib.DEFLATED,-15)
    block=compressor.compress(plain)+compressor.flush()
    container=bytearray(source[:start])
    struct.pack_into('<4I',container,0,magic,start,1,len(plain))
    container[16:16+count*4]=bytes(count*4)
    struct.pack_into('<I',container,16,len(block))
    return bytes(container)+block

def crypt(payload, seed):
    result = bytearray()
    for byte in payload:
        seed = seed * 16807 % 2147483647
        result.append(byte ^ (seed & 255))
    return bytes(result)

def decrypt(data):
    return crypt(data[4:], struct.unpack_from('<I', data)[0])

def encrypt(data, seed):
    return struct.pack('<I', seed) + crypt(data, seed)

class Reader:
    def __init__(self, data): self.data, self.pos = data, 0
    def read(self, fmt):
        value = struct.unpack_from('<' + fmt, self.data, self.pos)
        self.pos += struct.calcsize('<' + fmt)
        return value[0] if len(value) == 1 else value
    def tree(self):
        count, identifier = self.read('HI')
        return identifier, [self.node() for _ in range(count)]
    def node(self):
        kind = self.read('I')
        if kind in (16, 17, 19): return kind, self.tree()
        if kind in (0, 1, 6, 8, 9, 36): return kind, self.read('I')
        if kind in (2, 5, 7, 18, 32, 33, 34, 35, 37):
            size = self.read('I')
            value = self.data[self.pos:self.pos + size]
            if len(value) != size: raise ValueError('Truncated DTB string')
            self.pos += size
            return kind, value
        raise ValueError(f'Unsupported DTB node {kind}')

def write_tree(tree):
    identifier, nodes = tree
    return struct.pack('<HI', len(nodes), identifier) + b''.join(write_node(n) for n in nodes)

def write_node(node):
    kind, value = node
    if kind in (16, 17, 19): payload = write_tree(value)
    elif isinstance(value, bytes): payload = struct.pack('<I', len(value)) + value
    else: payload = struct.pack('<I', value)
    return struct.pack('<I', kind) + payload

def head(node, name):
    return node[0] in (16, 17, 19) and node[1][1][:1] == [(5, name)]

def patch_menu(plain, refresh_labels=False, assign_labels=True, with_cheats=False, refresh_unlock=True):
    if hashlib.sha256(plain).hexdigest() != MENU_SHA256:
        raise ValueError('Menu does not match the supported original dump')
    reader = Reader(plain)
    if reader.read('B') != 1: raise ValueError('Unexpected DTB version')
    tree = reader.tree()
    if reader.pos != len(plain) or b'\x01' + write_tree(tree) != plain:
        raise ValueError('DTB round-trip validation failed')
    panel = tree[1][0][1][1]
    if panel[:3] != [(5, b'new'), (5, b'GHPanel'), (5, b'main_panel')]:
        raise ValueError('Main-panel signature changed')
    enter = next(n[1][1] for n in panel if head(n, b'enter'))
    def command(*symbols):
        return 17, (0, [(5, s.encode('ascii')) for s in symbols])
    # Reuse existing menu components, preserving their font, colors and navigation.
    if assign_labels:
        enter.extend([command('main_leaderboards.btn', 'set_text', 'OPTIONS'),
                      command('main_options.btn', 'set_text', 'pause_quit')])
    if with_cheats:
        def block(kind,*nodes):return kind,(0,list(nodes))
        variable=(2,b'gh2_pc_unlock_all')
        applied=(2,b'gh2_pc_unlock_applied')
        commands=[
            block(17,(5,b'campaign'),(5,b'set_all_access'),variable),
            block(17,(5,b'set'),(2,b'cheat_enabled_unlock_all'),variable),
            block(17,(5,b'set'),(2,b'disable_save'),variable),
            block(17,(5,b'set'),(2,b'disable_career'),variable)]
        # The native All Access handler refreshes the song provider via this
        # panel method. Entry initializes it before our override, so refresh it
        # again after changing campaign access; also refresh on in-menu changes.
        if refresh_unlock:
            commands.append(block(17,(5,b'set'),applied,variable))
            commands.append(block(17,(2,b'this'),(5,b'reset_player_settings')))
        enable=block(17,(5,b'if'),block(17,(5,b'>='),variable,(0,0)),*commands)
        enter.append(enable)
        if refresh_unlock:
            poll=next(n[1][1] for n in panel if head(n,b'poll'))
            poll.append(block(17,(5,b'if'),block(17,(5,b'&&'),
                block(17,(5,b'>='),variable,(0,0)),
                block(17,(5,b'!'),block(17,(5,b'=='),applied,variable))),*commands))
    if refresh_labels:
        # The milo's focus animation restores its authored button text. Apply
        # the PC labels during panel polling as well as initial entry, using
        # the same native setter (which preserves focus colors and font).
        poll = next(n[1][1] for n in panel if head(n, b'poll'))
        poll.extend([command('main_leaderboards.btn', 'set_text', 'OPTIONS'),
                     command('main_options.btn', 'set_text', 'pause_quit')])
    select = next(n[1][1] for n in panel if head(n, b'SELECT_START_MSG'))
    switch = next(n[1][1] for n in select if head(n, b'switch'))
    leaderboards = next(n[1][1] for n in switch if head(n, b'main_leaderboards.btn'))
    options = next(n[1][1] for n in switch if head(n, b'main_options.btn'))
    if options[1][1][1] != [(5,b'ui'), (5,b'goto_screen'), (5,b'options_screen')]:
        raise ValueError('Options action signature changed')
    leaderboards[1:] = options[1:]
    options[1:] = [command('exit')]
    return b'\x01' + write_tree(tree)

def prepare(root, original_ark=None):
    directory = root / 'private/gen'
    header = directory / 'main.hdr'
    backup = directory / 'main.hdr.before-pc-menu'
    supplement = directory / 'main_1.ark'
    original = (backup if backup.exists() else header).read_bytes()
    if hashlib.sha256(original).hexdigest() != HDR_SHA256:
        raise ValueError('Archive index does not match the supported original dump')
    data = bytearray(decrypt(original))
    version, count, size_count, ark_size = struct.unpack_from('<4I', data)
    if (version, count, size_count) != (3,1,1): raise ValueError('Unsupported archive layout')
    ark = original_ark if original_ark is not None else directory / 'main_0.ark'
    if ark.stat().st_size != ark_size: raise ValueError('Original ARK size changed')
    reader = Reader(data); reader.pos = 16
    length = reader.read('I'); strings = data[reader.pos:reader.pos+length]; reader.pos += length
    names = []
    for _ in range(reader.read('I')):
        offset = reader.read('I'); names.append(bytes(strings[offset:strings.index(0, offset)]))
    matches = {}
    for _ in range(reader.read('I')):
        position = reader.pos
        offset, name, folder, length, zero = reader.read('5I')
        path=names[folder] + b'/' + names[name]
        if path in (b'ui/gen/main.dtb',b'ui/gen/main.milo_xbox'):
            matches[path]=(position, offset, length)
    if reader.pos != len(data) or len(matches) != 2: raise ValueError('Archive index validation failed')
    position, offset, length = matches[b'ui/gen/main.dtb']
    with ark.open('rb') as file:
        file.seek(offset); source = file.read(length)
        _,milo_offset,milo_length=matches[b'ui/gen/main.milo_xbox']
        file.seek(milo_offset);milo_source=file.read(milo_length)
    def make_header(parts):
        updated=bytearray(data);cursor=ark_size
        for path,part in parts:
            record=matches[path][0]
            struct.pack_into('<I',updated,record,cursor)
            struct.pack_into('<I',updated,record+12,len(part));cursor+=len(part)
        struct.pack_into('<II',updated,4,2,2)
        updated[16:16]=struct.pack('<I',cursor-ark_size)
        return encrypt(updated,struct.unpack_from('<I',original)[0])
    payload=encrypt(patch_menu(decrypt(source),assign_labels=False,with_cheats=True),struct.unpack_from('<I',source)[0])
    parts=[(b'ui/gen/main.dtb',payload),(b'ui/gen/main.milo_xbox',patch_milo(milo_source))]
    patched=make_header(parts)
    legacy_headers=[]
    old_cheat_asset=encrypt(patch_menu(decrypt(source),assign_labels=False,with_cheats=True,refresh_unlock=False),struct.unpack_from('<I',source)[0])
    legacy_headers.append(make_header([(b'ui/gen/main.dtb',old_cheat_asset),parts[1]]))
    old_asset=encrypt(patch_menu(decrypt(source),assign_labels=False),struct.unpack_from('<I',source)[0])
    legacy_headers.append(make_header([(b'ui/gen/main.dtb',old_asset),parts[1]]))
    for refresh in (False,True):
        old=encrypt(patch_menu(decrypt(source),refresh_labels=refresh),struct.unpack_from('<I',source)[0])
        legacy_headers.append(make_header([(b'ui/gen/main.dtb',old)]))
    # Presence additions reuse the existing supplemental archive, preserving
    # the Options/Quit and cheats patches. Accept the preceding menu version.
    import sys
    if str(Path(__file__).resolve().parent) not in sys.path:sys.path.insert(0,str(Path(__file__).resolve().parent))
    from prepare_discord_presence import prepare_parts,patch_script
    prior_presence,prior_index=prepare_parts(root,ark,with_venue=False)
    original_parts=parts.copy()
    for path,contents in prior_presence:matches[path]=prior_index[path]
    legacy_headers.append(make_header([(original_parts[0][0],patch_script(original_parts[0][1],'ui/gen/main.dtb',with_venue=False)[0]),original_parts[1],*prior_presence]))
    presence_parts,presence_index=prepare_parts(root,ark)
    legacy_headers.append(patched)
    parts[0]=(parts[0][0],patch_script(parts[0][1],'ui/gen/main.dtb')[0])
    for path,contents in presence_parts:
        matches[path]=presence_index[path]
        parts.append((path,contents))
    patched=make_header(parts)
    payload=b''.join(part for _,part in parts)
    if not backup.exists():
        if supplement.exists(): raise ValueError('Refusing to replace an existing second archive')
        backup.write_bytes(original)
    elif header.read_bytes() not in (original, patched, *legacy_headers):
        raise ValueError('Archive index has unrelated changes; leaving it untouched')
    # Publish data before its index. Atomic replacement also breaks audit hardlinks.
    for target, contents in ((supplement,payload),(header,patched)):
        if target.exists() and target.read_bytes() == contents: continue
        temporary = target.with_name(target.name + '.pc-menu-tmp')
        temporary.write_bytes(contents); temporary.replace(target)
    print(f'Native PC menu prepared: Options then Quit; supplemental archive {len(payload)} bytes.')

if __name__ == '__main__': prepare(Path(__file__).resolve().parents[1])
