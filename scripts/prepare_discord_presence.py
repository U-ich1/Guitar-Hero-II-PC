"""Add read-only presence snapshots to the user's native screen scripts."""
from pathlib import Path
import json,struct
from prepare_pc_menu import Reader,decrypt,encrypt,write_tree

def block(*nodes):return 17,(0,list(nodes))
def sym(value):return 5,value.encode()
def var(value):return 2,value.encode()
def call(*values):return block(*(sym(v) for v in values))
def fields(status,with_venue=True):
    result=[block(sym('set'),var('gh2_rpc_mode'),call('gamecfg','get','mode')),
        block(sym('set'),var('gh2_rpc_song'),call('gamecfg','get_song') if status else sym('none')),
        block(sym('set'),var('gh2_rpc_difficulty'),block(sym('game'),sym('get_difficulty_sym'),(0,0)) if status else sym('none')),
        block(sym('set'),var('gh2_rpc_status'),(0,status))]
    if with_venue:result.insert(-1,block(sym('set'),var('gh2_rpc_venue'),call('game','get_venue') if status else sym('none')))
    return result

def patch_script(raw,path,with_venue=True):
    plain=decrypt(raw);r=Reader(plain)
    if r.read('B')!=1:raise ValueError('Unexpected DTB version: '+path)
    tree=r.tree()
    if r.pos!=len(plain)or b'\x01'+write_tree(tree)!=plain:raise ValueError('DTB round trip failed: '+path)
    changed=0
    for kind,value in tree[1]:
        if kind!=17:continue
        nodes=value[1]
        if len(nodes)<3 or nodes[0]!=sym('new')or nodes[1]!=sym('GHScreen')or nodes[2][0]!=5:continue
        name=nodes[2][1].decode()
        status=1 if name in ('game_screen','practice_game_screen') else 2 if ('pause' in name or name.startswith('practice_change_speed')) else 3 if name in ('post_show_screen','endgame_screen','lose_screen','practice_end_screen') else 0
        if name=='game_screen': commands=[block(sym('if_else'),block(var('this'),sym('get'),sym('attract_mode')),
            block(sym('do'),*fields(0,with_venue)),block(sym('do'),*fields(1,with_venue)))]
        else:commands=fields(status,with_venue)
        # Keep inherited enter handlers (pause/world state, venue setup, and
        # error dialogs) intact. Poll snapshots run after the original logic.
        for event in ('poll',):
            handler=next((n[1][1] for n in nodes[3:] if n[0]==16 and n[1][1][:1]==[sym(event)]),None)
            # Paused/settings screens inherit entry handlers from macros; poll
            # captures mode and difficulty changes without replacing those macros.
            if handler is None:nodes.append((16,(0,[sym(event),*commands])))
            else:handler.extend(commands)
        changed+=1
    if not changed:return raw,0
    return encrypt(b'\x01'+write_tree(tree),struct.unpack_from('<I',raw)[0]),changed

def entries(root):
    directory=root/'private/gen';backup=directory/'main.hdr.before-pc-menu'
    r=Reader(decrypt((backup if backup.exists() else directory/'main.hdr').read_bytes()));r.pos=16
    length=r.read('I');strings=r.data[r.pos:r.pos+length];r.pos+=length
    names=[]
    for _ in range(r.read('I')):
        offset=r.read('I');names.append(strings[offset:strings.index(0,offset)])
    result={}
    for _ in range(r.read('I')):
        record=r.pos;offset,name,folder,size,zero=r.read('5I');result[names[folder]+b'/'+names[name]]=(record,offset,size)
    return result

def prepare_parts(root,ark=None,with_venue=True):
    index=entries(root);directory=root/'private/gen';parts=[];screens=0;songs={}
    with (ark or directory/'main_0.ark').open('rb') as stream:
        for path,(record,offset,size) in index.items():
            if not ((path.startswith(b'ui/')and path.endswith(b'.dtb'))or path==b'config/gen/songs.dtb'):continue
            stream.seek(offset);raw=stream.read(size)
            if path==b'config/gen/songs.dtb':
                reader=Reader(decrypt(raw));reader.read('B');tree=reader.tree()
                for kind,value in tree[1]:
                    if kind!=16 or not value[1]or value[1][0][0]!=5:continue
                    key=value[1][0][1].decode();title=next((n[1][1][1][1].decode() for n in value[1][1:] if n[0]==16 and n[1][1][:1]==[sym('name')]),key)
                    songs[key]=title
                continue
            # Main is already patched for Options/Quit/cheats by the caller.
            if path==b'ui/gen/main.dtb':continue
            patched,count=patch_script(raw,path.decode(),with_venue);screens+=count
            if count:parts.append((path,patched))
    output=root/'build/discord-songs.tsv';output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text('\n'.join(k+'\t'+v.replace('\t',' ').replace('\n',' ') for k,v in songs.items()),encoding='utf-8')
    (root/'docs').mkdir(exist_ok=True)
    (root/'docs/discord_script_validation.json').write_text(json.dumps(dict(screens=screens,songs=len(songs),scope='Parsed and round-tripped original native scripts; gameplay execution still needs observation.'),indent=2))
    return parts,index
