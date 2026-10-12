"""Native menu transformations and safe supplemental-archive installation."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('pc_menu',Path(__file__).resolve().parents[1]/'scripts/prepare_pc_menu.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def sym(text):return (5,text.encode())
def group(kind,*nodes):return kind,(123,list(nodes))
def fixture():
    options=group(16,sym('main_options.btn'),group(17,sym('ui'),sym('goto_screen'),sym('options_screen')))
    board=group(16,sym('main_leaderboards.btn'),group(17,sym('old_action')))
    career=group(16,sym('main_career.btn'),group(17,sym('career_action')))
    return b'\x01'+m.write_tree((1,[group(17,sym('new'),sym('GHPanel'),sym('main_panel'),
        group(16,sym('enter'),group(17,sym('existing_enter'))),
        group(16,sym('SELECT_START_MSG'),group(17,sym('switch'),(2,b'component'),career,board,options)),
        group(16,sym('poll'),group(17,sym('existing_poll'))))]))

class MenuTests(unittest.TestCase):
    def setUp(self):
        self.plain=fixture();self.guard=patch.object(m,'MENU_SHA256',hashlib.sha256(self.plain).hexdigest());self.guard.start()
    def tearDown(self):self.guard.stop()
    def test_transform_preserves_other_actions(self):
        result=m.patch_menu(self.plain,assign_labels=False)
        self.assertIn(b'existing_enter',result);self.assertIn(b'existing_poll',result)
        self.assertIn(b'career_action',result);self.assertNotIn(b'old_action',result)
        self.assertEqual(result.count(b'options_screen'),1)
        self.assertNotIn(b'set_text',result)
        r=m.Reader(result);self.assertEqual(r.read('B'),1);tree=r.tree()
        self.assertEqual(r.pos,len(result));self.assertEqual(b'\x01'+m.write_tree(tree),result)
        panel=tree[1][0][1][1]
        poll=next(n[1][1] for n in panel if m.head(n,b'poll'))
        self.assertEqual(poll,[sym('poll'),group(17,sym('existing_poll'))])
    def test_wrong_dump_rejected(self):
        with self.assertRaises(ValueError):m.patch_menu(self.plain+b'wrong')
    def test_unlock_entry_and_live_menu_refresh(self):
        result=m.patch_menu(self.plain,assign_labels=False,with_cheats=True)
        r=m.Reader(result);r.read('B');panel=r.tree()[1][0][1][1]
        enter=next(n[1][1] for n in panel if m.head(n,b'enter'))[-1]
        poll=next(n[1][1] for n in panel if m.head(n,b'poll'))[-1]
        variables={b'gh2_pc_unlock_all':1};state={'access':0,'list':0,'refreshes':0}
        def evaluate(node):
            kind,value=node
            if kind==0:return value
            if kind==2:return variables.get(value,0)
            if kind==5:return value
            args=value[1];operation=args[0][1]
            if operation==b'if':
                if evaluate(args[1]):
                    for argument in args[2:]:evaluate(argument)
            elif operation==b'&&':return evaluate(args[1]) and evaluate(args[2])
            elif operation==b'>=':return evaluate(args[1])>=evaluate(args[2])
            elif operation==b'==':return evaluate(args[1])==evaluate(args[2])
            elif operation==b'!':return not evaluate(args[1])
            elif operation==b'set':variables[args[1][1]]=evaluate(args[2])
            elif operation==b'campaign':
                self.assertEqual(args[1],sym('set_all_access'));state['access']=evaluate(args[2])
            elif operation==b'this':
                self.assertEqual(args[1],sym('reset_player_settings'))
                state['list']=state['access'];state['refreshes']+=1
            else:self.fail('Unexpected injected script operation: '+str(operation))
        evaluate(enter)
        self.assertEqual((state['access'],state['list'],state['refreshes']),(1,1,1))
        for key in (b'cheat_enabled_unlock_all',b'disable_save',b'disable_career'):self.assertEqual(variables[key],1)
        for _ in range(100):evaluate(poll)
        self.assertEqual(state['refreshes'],1)
        variables[b'gh2_pc_unlock_all']=0;evaluate(poll)
        self.assertEqual((state['access'],state['list'],state['refreshes']),(0,0,2))
        for key in (b'cheat_enabled_unlock_all',b'disable_save',b'disable_career'):self.assertEqual(variables[key],0)
        variables[b'gh2_pc_unlock_all']=1;evaluate(poll)
        self.assertEqual((state['access'],state['list'],state['refreshes']),(1,1,3))
        variables[b'gh2_pc_unlock_all']=-1;evaluate(poll);evaluate(enter)
        self.assertEqual((state['access'],state['list'],state['refreshes']),(1,1,3))
    def test_archive_install_idempotent_and_original_preserved(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1]/"build") as tmp:
            root=Path(tmp);d=root/'private/gen';d.mkdir(parents=True)
            dtb=m.encrypt(self.plain,12345)
            labels=b'prefix'+b''.join(b'\x01'+struct.pack('<I',len(s))+s+b'\x01' for s in (b'leaderboards',b'OPTIONS'))+b'suffix'
            c=zlib.compressobj(wbits=-15);block=c.compress(labels)+c.flush()
            milo=struct.pack('<5I',0xCBBEDEAF,32,1,len(labels),len(block))+bytes(12)+block
            ark=dtb+milo;(d/'main_0.ark').write_bytes(ark)
            strings=b'\0main.dtb\0ui/gen\0main.milo_xbox\0'
            hdr=struct.pack('<5I',3,1,1,len(ark),len(strings))+strings
            hdr+=struct.pack('<5I',4,0,1,10,17)+struct.pack('<I',2)
            hdr+=struct.pack('<5I',0,1,2,len(dtb),0)+struct.pack('<5I',len(dtb),3,2,len(milo),0)
            original=m.encrypt(hdr,54321);(d/'main.hdr').write_bytes(original)
            with patch.object(m,'HDR_SHA256',hashlib.sha256(original).hexdigest()),patch.object(m,'MILO_SHA256',hashlib.sha256(milo).hexdigest()):
                m.prepare(root);first=(d/'main.hdr').read_bytes();m.prepare(root)
                self.assertEqual((d/'main.hdr').read_bytes(),first)
                self.assertEqual((d/'main_0.ark').read_bytes(),ark)
                self.assertEqual((d/'main.hdr.before-pc-menu').read_bytes(),original)
                decoded=m.decrypt(first)
                self.assertEqual(struct.unpack_from('<3I',decoded),(3,2,2))
                payload=(d/'main_1.ark').read_bytes()
                expected=m.patch_menu(self.plain,assign_labels=False,with_cheats=True)
                self.assertEqual(m.decrypt(payload[:len(expected)+4]),expected)
                asset=payload[len(expected)+4:]
                raw=zlib.decompress(asset[32:],-15)
                self.assertNotIn(b'leaderboards',raw);self.assertIn(b'pause_quit',raw)
                self.assertTrue(raw.startswith(b'prefix'));self.assertTrue(raw.endswith(b'suffix'))
                (d/'main.hdr').write_bytes(b'unrelated user modification')
                with self.assertRaises(ValueError):m.prepare(root)
                self.assertEqual((d/'main.hdr').read_bytes(),b'unrelated user modification')

if __name__=='__main__':unittest.main()

