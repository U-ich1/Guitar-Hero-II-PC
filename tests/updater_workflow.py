"""Release verification and isolated source-update/build integration checks."""
from pathlib import Path
import contextlib, hashlib, io, json, os, shutil, sys, tempfile, threading, unittest, zipfile
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
import gui_updater as updater

class UpdaterTests(unittest.TestCase):
    def setUp(self):
        (ROOT/'build').mkdir(exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=ROOT/'build');self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.cancel=threading.Event()
    def package(self,extra=None):
        files={'Build.ps1':b"New-Item -ItemType Directory -Path build -Force | Out-Null\nSet-Content build/build-complete.txt 'done'\nSet-Content build/build-receipt.json '{\"source_id\":\"fixture\"}'\n",
               'Update.ps1':(ROOT/'Update.ps1').read_bytes(),
               'scripts/update_source.py':(ROOT/'scripts/update_source.py').read_bytes(),
               'scripts/build_project.py':(ROOT/'scripts/build_project.py').read_bytes(),
               'dependencies.lock.json':b'{"repositories": []}','runtime/replaced.h':b'new source'}
        if extra:files.update(extra)
        manifest={'schema':1,'source_id':'a'*64,'files':{n:hashlib.sha256(b).hexdigest() for n,b in files.items()}}
        output=io.BytesIO()
        with zipfile.ZipFile(output,'w') as z:
            for n,b in files.items():z.writestr(n,b)
            z.writestr('SOURCE-MANIFEST.json',json.dumps(manifest))
        return output.getvalue()
    def release(self,data=None,tag='v0.1.0'):
        data=self.package() if data is None else data
        return {'tag_name':tag,'published_at':'2026-10-12','draft':False,'prerelease':False,'assets':[{'name':'gh2pc-source-0.1.0.zip','size':len(data),'state':'uploaded','digest':'sha256:'+hashlib.sha256(data).hexdigest(),'browser_download_url':updater.DOWNLOAD_PREFIX+tag+'/source.zip'}]}
    def test_latest_selection_and_preview_filter(self):
        stable=self.release();other=self.release(tag='v0.0.9');other['published_at']='2026-10-13'
        preview=self.release(tag='v0.2.0-preview');preview['prerelease']=True
        draft=self.release(tag='draft');draft['draft']=True
        def response(url):return {'tag_name':'v0.1.0'} if url.endswith('/latest') else [other,stable,preview,draft]
        with patch.object(updater,'get_json',side_effect=response):
            rows,latest=updater.catalog();self.assertEqual(latest,'v0.1.0');self.assertEqual({r['tag_name'] for r in rows},{'v0.1.0','v0.0.9'})
            rows,_=updater.catalog(True);self.assertIn(preview,rows);self.assertNotIn(draft,rows)
    def test_verified_download_and_progress(self):
        data=self.package();events=[]
        with patch.object(updater,'request',return_value=io.BytesIO(data)):
            file=updater.download_release(self.release(data),self.root,lambda *a:events.append(a),self.cancel)
        self.assertEqual(file.read_bytes(),data);self.assertEqual(events[-1],('progress',100));self.assertFalse((self.root/'source.zip.partial').exists())
    def test_corrupt_and_cancelled_download_do_not_install(self):
        data=self.package()
        with patch.object(updater,'request',return_value=io.BytesIO(data[:-1]+b'x')):
            with self.assertRaises(RuntimeError):updater.download_release(self.release(data),self.root,lambda *a:None,self.cancel)
        self.assertFalse((self.root/'source.zip').exists());self.assertFalse((self.root/'source.zip.partial').exists())
        self.cancel.set()
        with patch.object(updater,'request',return_value=io.BytesIO(data)):
            with self.assertRaises(updater.Cancelled):updater.download_release(self.release(data),self.root,lambda *a:None,self.cancel)
        self.assertFalse((self.root/'source.zip').exists())
    def test_checksum_sidecar_exact_filename(self):
        data=self.package();release=self.release(data);asset=release['assets'][0];asset.pop('digest')
        release['assets'].append({'name':'SHA256SUMS.txt','browser_download_url':updater.DOWNLOAD_PREFIX+'v0.1.0/SHA256SUMS.txt'})
        digest=hashlib.sha256(data).hexdigest()
        with patch.object(updater,'request',return_value=io.BytesIO((digest+'  '+asset['name']+'\n').encode())):self.assertEqual(updater.expected_digest(release,asset),digest)
        with patch.object(updater,'request',return_value=io.BytesIO((digest+'  wrong.zip\n').encode())):
            with self.assertRaises(RuntimeError):updater.expected_digest(release,asset)
    def test_unsafe_archive_and_urls(self):
        for name in ('../outside.cpp','private/default.xex','runtime/../../outside.cpp','runtime/link.exe','C:/outside.cpp'):
            file=self.root/'unsafe.zip';file.write_bytes(self.package({name:b'bad'}))
            with self.assertRaises(RuntimeError):updater.validate_archive(file)
        for url in ('http://github.com/x','https://github.com.evil.test/x','https://user@github.com/x','https://example.com/x'):
            self.assertFalse(updater.trusted_url(url))
        release=self.release();release['assets'][0]['browser_download_url']='https://github.com/another/repo/releases/download/v1/x.zip'
        with self.assertRaises(RuntimeError):updater.source_asset(release)
    def test_process_guard_is_scoped_to_project(self):
        updater.check_idle(self.root,[('Guitar Hero 2.exe',ROOT/'tools/other/game.exe')])
        with self.assertRaises(RuntimeError):updater.check_idle(self.root,[('Guitar Hero 2.exe',self.root/'tools/game.exe')])
    @unittest.skipUnless(os.name=='nt','Windows lock')
    def test_install_lock_and_stale_file_recovery(self):
        with updater.InstallLock(self.root):
            with self.assertRaises(RuntimeError):
                with updater.InstallLock(self.root):pass
        with updater.InstallLock(self.root):pass
    @unittest.skipUnless(os.name=='nt','Windows update integration')
    def test_real_source_apply_build_and_failure_receipt(self):
        archive=self.root/'selected.zip';archive.write_bytes(self.package())
        (self.root/'private').mkdir();(self.root/'private/default.xex').write_bytes(b'keep dump')
        (self.root/'runtime').mkdir();(self.root/'runtime/replaced.h').write_bytes(b'old source')
        (self.root/'dependencies.lock.json').write_text('{"repositories": []}')
        (self.root/'Update.ps1').write_bytes((ROOT/'Update.ps1').read_bytes())
        (self.root/'scripts').mkdir()
        for name in ('update_source.py','build_project.py'):(self.root/'scripts'/name).write_bytes((ROOT/'scripts'/name).read_bytes())
        (self.root/'Documents').mkdir();(self.root/'Documents/save.dat').write_bytes(b'keep save')
        log=self.root/'update.log'
        with patch.object(updater,'check_idle'):
            updater.run_update(self.root,archive,self.release(),lambda *a:None,log)
        self.assertEqual((self.root/'runtime/replaced.h').read_bytes(),b'new source')
        self.assertEqual((self.root/'private/default.xex').read_bytes(),b'keep dump')
        self.assertEqual((self.root/'Documents/save.dat').read_bytes(),b'keep save')
        self.assertTrue((self.root/'build/build-complete.txt').exists())
        receipt=self.root/'build/updater/installed-release.json';before=receipt.read_bytes()
        failing=self.root/'failing.zip';failing.write_bytes(self.package({'Build.ps1':b'exit 7'}))
        with patch.object(updater,'check_idle'):
            with self.assertRaises(RuntimeError):updater.run_update(self.root,failing,self.release(tag='v0.2.0'),lambda *a:None,log)
        self.assertEqual(receipt.read_bytes(),before)
        self.assertTrue(list((self.root/'build/source-update-backups').rglob('replaced.h')))

if __name__=='__main__':unittest.main()
