"""Security/correctness checks for source packaging, updates and dump validation."""
from pathlib import Path
import hashlib,json,sys,tempfile,unittest,zipfile
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
import build_project,update_source

class WorkflowTests(unittest.TestCase):
    def setUp(self):
        (ROOT/'build').mkdir(exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=ROOT/'build');self.root=Path(self.temp.name).resolve()
        assert self.root.is_relative_to((ROOT/'build').resolve())
        self.addCleanup(self.temp.cleanup)
        self.settings=b'preserve saved settings';(self.root/'private').mkdir();(self.root/'private/default.xex').write_bytes(b'preserve game dump')
        (self.root/'runtime').mkdir();(self.root/'runtime/test.cpp').write_bytes(b'old source')
    def archive(self,name,files,hash_override=None):
        file=self.root/name
        manifest=dict(schema=1,source_id='test',files={key:hashlib.sha256(value).hexdigest() for key,value in files.items()})
        if hash_override:manifest['files'].update(hash_override)
        with zipfile.ZipFile(file,'w') as archive:
            for key,value in files.items():archive.writestr(key,value)
            archive.writestr('SOURCE-MANIFEST.json',json.dumps(manifest))
        return file
    def test_update_and_backup_leave_dump_untouched(self):
        archive=self.archive('update.zip',{'runtime/test.cpp':b'new source'})
        with patch.object(update_source,'ROOT',self.root):update_source.apply_archive(archive)
        self.assertEqual((self.root/'runtime/test.cpp').read_bytes(),b'new source')
        self.assertEqual((self.root/'private/default.xex').read_bytes(),b'preserve game dump')
        backups=list((self.root/'build/source-update-backups').rglob('test.cpp'))
        self.assertEqual(len(backups),1);self.assertEqual(backups[0].read_bytes(),b'old source')
    def test_tampered_source_rejected_before_write(self):
        archive=self.archive('bad.zip',{'runtime/test.cpp':b'new source'},{'runtime/test.cpp':'0'*64})
        with patch.object(update_source,'ROOT',self.root):
            with self.assertRaises(RuntimeError):update_source.apply_archive(archive)
        self.assertEqual((self.root/'runtime/test.cpp').read_bytes(),b'old source')
    def test_custom_launcher_artwork_updates(self):
        name='launcher/assets/gh2pc-original-logo.png';archive=self.archive('art.zip',{name:b'custom launcher artwork'})
        with patch.object(update_source,'ROOT',self.root):update_source.apply_archive(archive)
        self.assertEqual((self.root/name).read_bytes(),b'custom launcher artwork')
    def test_removed_tracked_docs_retired_but_edits_and_dump_preserved(self):
        (self.root/'docs').mkdir()
        original=b'old guide'
        (self.root/'docs/retired.md').write_bytes(original)
        (self.root/'docs/edited.md').write_bytes(b'my edits')
        previous={'schema':1,'files':{'docs/retired.md':hashlib.sha256(original).hexdigest(),'docs/edited.md':hashlib.sha256(original).hexdigest(),'private/default.xex':hashlib.sha256(b'preserve game dump').hexdigest()}}
        (self.root/'SOURCE-MANIFEST.json').write_text(json.dumps(previous))
        archive=self.archive('retire.zip',{'runtime/test.cpp':b'new source'})
        with patch.object(update_source,'ROOT',self.root):update_source.apply_archive(archive)
        self.assertFalse((self.root/'docs/retired.md').exists())
        self.assertEqual((self.root/'docs/edited.md').read_bytes(),b'my edits')
        self.assertEqual((self.root/'private/default.xex').read_bytes(),b'preserve game dump')
        self.assertEqual(next((self.root/'build/source-update-backups').rglob('retired.md')).read_bytes(),original)
    def test_traversal_and_game_asset_paths_rejected(self):
        for key in ('../escape.cpp','private/default.xex','tools/compiler.exe','runtime/../../private/default.xex','C:/escape.cpp'):
            archive=self.archive('unsafe.zip',{key:b'payload'})
            with patch.object(update_source,'ROOT',self.root):
                with self.assertRaises(RuntimeError):update_source.apply_archive(archive)
        self.assertEqual((self.root/'private/default.xex').read_bytes(),b'preserve game dump')
    def test_tool_archive_paths_and_checksum(self):
        archive=self.root/'tool.zip'
        with zipfile.ZipFile(archive,'w') as output:output.writestr('../escape','payload')
        with self.assertRaises(RuntimeError):build_project.safe_extract(archive,self.root/'extract')
        cache=self.root/'tools/downloads';cache.mkdir(parents=True);(cache/'fake.zip').write_bytes(b'bad archive')
        with patch.object(build_project,'ROOT',self.root):
            with self.assertRaises(RuntimeError):build_project.setup_archive(dict(name='fake',directory='fake',probe='bin/tool.exe',sha256='0'*64),True)
    def test_unknown_dump_revision_rejected(self):
        with self.assertRaises(RuntimeError):build_project.validate_dump(self.root/'private')
    def test_source_identity_ignores_platform_line_endings(self):
        file=self.root/'runtime/test.cpp';file.write_bytes(b'first\nsecond\n')
        with patch.object(build_project,'ROOT',self.root):
            original=build_project.source_id();file.write_bytes(b'first\r\nsecond\r\n');self.assertEqual(original,build_project.source_id())
    def test_source_allowlist_excludes_derived_assets(self):
        paths=[file.relative_to(ROOT).as_posix() for file in build_project.source_files()]
        self.assertTrue(paths)
        self.assertFalse(any(path.startswith(('private/','tools/','generated/','build/')) for path in paths))
        self.assertFalse(any(path.lower().endswith(('.iso','.exe','.dll','.ico','.png')) and path not in build_project.SOURCE_ARTWORK for path in paths))

if __name__=='__main__':unittest.main()
