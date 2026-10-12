"""Graphical release downloader and source-build updater for Windows."""
from pathlib import Path, PurePosixPath
import argparse, ctypes, hashlib, io, json, os, queue, re, shutil, subprocess, sys
import threading, time, urllib.request, urllib.parse, zipfile
import urllib.error

REPOSITORY = 'U-ich1/Guitar-Hero-II-PC'
API = 'https://api.github.com/repos/' + REPOSITORY
DOWNLOAD_PREFIX = 'https://github.com/' + REPOSITORY + '/releases/download/'
MAX_BYTES = 128 * 1024 * 1024
ROOT_FILES = {'Build.ps1','Update.ps1','CMakeLists.txt','README.md','LICENSE',
              'Start-GH2.cmd','Start-GH2.ps1','dependencies.lock.json','requirements-build.txt','.gitignore'}
ARTWORK = {'launcher/assets/gh2pc-original-logo.png','launcher/assets/gh2pc-original-icon.png','launcher/assets/gh2pc-original.ico'}
SOURCE_DIRS = {'runtime','launcher','patches','scripts','tests','config'}
EXTENSIONS = {'.py','.ps1','.cpp','.h','.inl','.toml','.md','.rc'}

class Cancelled(RuntimeError): pass

def trusted_url(url):
    parsed = urllib.parse.urlsplit(url)
    return (parsed.scheme == 'https' and not parsed.username and not parsed.password
            and parsed.port in (None,443) and parsed.hostname in
            {'github.com','api.github.com','release-assets.githubusercontent.com','objects.githubusercontent.com'})

class SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not trusted_url(newurl): raise RuntimeError('Download redirected outside GitHub.')
        return super().redirect_request(req,fp,code,msg,headers,newurl)

def request(url):
    if not trusted_url(url): raise RuntimeError('Untrusted download address.')
    req = urllib.request.Request(url,headers={'User-Agent':'GH2PC-Updater','Accept':'application/vnd.github+json'})
    return urllib.request.build_opener(SafeRedirect()).open(req,timeout=20)

def get_json(url):
    try:
        with request(url) as response:
            data = response.read(4*1024*1024+1)
    except urllib.error.HTTPError as error:
        if error.code in (403,429): raise RuntimeError('GitHub release checks are temporarily rate-limited. Try again later.') from error
        raise
    if len(data)>4*1024*1024: raise RuntimeError('Release response is too large.')
    return json.loads(data.decode('utf-8'))

def source_asset(release):
    candidates = [a for a in release.get('assets',[]) if re.fullmatch(r'(gh2pc-source-|gh2-recomp-source-).+\.zip',a.get('name','')) and a.get('state','uploaded')=='uploaded']
    if len(candidates)!=1: raise RuntimeError('This release does not have one supported source package.')
    asset = candidates[0]
    if not asset.get('browser_download_url','').startswith(DOWNLOAD_PREFIX): raise RuntimeError('Source package is outside the project repository.')
    if not 0<int(asset.get('size',0))<=MAX_BYTES: raise RuntimeError('Source package has an invalid size.')
    return asset

def catalog(include_preview=False):
    releases=[]
    for page in range(1,11):
        rows=get_json(API+'/releases?per_page=100&page='+str(page))
        if not isinstance(rows,list): raise RuntimeError('GitHub returned an invalid release list.')
        for row in rows:
            if row.get('draft') or (row.get('prerelease') and not include_preview): continue
            try: source_asset(row)
            except (RuntimeError,ValueError): continue
            releases.append(row)
        if len(rows)<100: break
    if not releases: raise RuntimeError('No compatible published releases are available.')
    try: latest=get_json(API+'/releases/latest').get('tag_name')
    except urllib.error.HTTPError as error:
        if error.code!=404: raise
        latest=None
    releases.sort(key=lambda row:row.get('published_at',''),reverse=True)
    return releases,latest

def expected_digest(release,asset):
    digest=asset.get('digest') or ''
    if re.fullmatch(r'sha256:[0-9a-fA-F]{64}',digest): return digest[7:].lower()
    checks=[a for a in release.get('assets',[]) if a.get('name')=='SHA256SUMS.txt' or a.get('name')==asset['name']+'.sha256']
    for checksum in checks:
        url=checksum.get('browser_download_url','')
        if not url.startswith(DOWNLOAD_PREFIX): raise RuntimeError('Checksum is outside the project repository.')
        with request(url) as response: text=response.read(65537)
        if len(text)>65536: raise RuntimeError('Checksum file is too large.')
        for line in text.decode('utf-8-sig').splitlines():
            match=re.fullmatch(r'([0-9a-fA-F]{64})\s+\*?(.+)',line.strip())
            if match and match[2]==asset['name']: return match[1].lower()
    raise RuntimeError('This release has no usable source-package checksum.')

def download_release(release,directory,emit,cancel):
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    asset=source_asset(release);digest=expected_digest(release,asset)
    partial=directory/'source.zip.partial';destination=directory/'source.zip'
    total=0;hasher=hashlib.sha256()
    try:
        with request(asset['browser_download_url']) as response,partial.open('wb') as output:
            while True:
                if cancel.is_set(): raise Cancelled('Download cancelled.')
                chunk=response.read(65536)
                if not chunk: break
                total+=len(chunk)
                if total>MAX_BYTES or total>asset['size']: raise RuntimeError('Download exceeds the expected size.')
                output.write(chunk);hasher.update(chunk);emit('progress',100*total/asset['size'])
        if total!=asset['size'] or hasher.hexdigest()!=digest: raise RuntimeError('Download verification failed. No project files were changed.')
        if cancel.is_set(): raise Cancelled('Download cancelled.')
        validate_archive(partial)
        os.replace(partial,destination)
        return destination
    finally:
        if partial.exists(): partial.unlink()

def allowed_source(name):
    p=PurePosixPath(name)
    if not p.parts or p.is_absolute() or '..' in p.parts or '\\' in name or ':' in name: return False
    return name in ROOT_FILES or name in ARTWORK or (p.parts[0] in SOURCE_DIRS and p.suffix.lower() in EXTENSIONS) or (p.parts[0]=='docs' and p.suffix=='.md')

def validate_archive(path):
    with zipfile.ZipFile(path) as package:
        entries=package.infolist()
        if len(entries)>5000 or sum(e.file_size for e in entries)>MAX_BYTES: raise RuntimeError('Source archive exceeds supported limits.')
        if any((e.external_attr>>16)&0o170000==0o120000 for e in entries): raise RuntimeError('Source archive contains links.')
        manifest=json.loads(package.read('SOURCE-MANIFEST.json'))
        if manifest.get('schema')!=1 or not isinstance(manifest.get('files'),dict): raise RuntimeError('Unsupported source manifest.')
        expected=set(manifest['files'])|{'SOURCE-MANIFEST.json'}
        if len(entries)!=len(expected) or set(package.namelist())!=expected: raise RuntimeError('Source archive contains unexpected or duplicate files.')
        for name,digest in manifest['files'].items():
            if not allowed_source(name) or not isinstance(digest,str) or not re.fullmatch('[0-9a-f]{64}',digest): raise RuntimeError('Invalid source manifest entry: '+name)
            if hashlib.sha256(package.read(name)).hexdigest()!=digest: raise RuntimeError('Source file verification failed: '+name)
        for required in ('Build.ps1','Update.ps1','scripts/update_source.py','scripts/build_project.py','dependencies.lock.json'):
            if required not in manifest['files']: raise RuntimeError('Incomplete source package: '+required)
        return manifest

def windows_processes():
    from ctypes import wintypes as w
    class Entry(ctypes.Structure):
        _fields_=[('dwSize',w.DWORD),('cntUsage',w.DWORD),('th32ProcessID',w.DWORD),('th32DefaultHeapID',ctypes.c_size_t),('th32ModuleID',w.DWORD),('cntThreads',w.DWORD),('th32ParentProcessID',w.DWORD),('pcPriClassBase',w.LONG),('dwFlags',w.DWORD),('szExeFile',w.WCHAR*260)]
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.CreateToolhelp32Snapshot.restype=w.HANDLE
    kernel.OpenProcess.restype=w.HANDLE
    kernel.CloseHandle.argtypes=[w.HANDLE]
    kernel.Process32FirstW.argtypes=[w.HANDLE,ctypes.POINTER(Entry)]
    kernel.Process32NextW.argtypes=[w.HANDLE,ctypes.POINTER(Entry)]
    kernel.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,ctypes.POINTER(w.DWORD)]
    snapshot=kernel.CreateToolhelp32Snapshot(2,0)
    if snapshot==ctypes.c_void_p(-1).value: raise RuntimeError('Could not check running applications.')
    try:
        entry=Entry();entry.dwSize=ctypes.sizeof(entry)
        valid=kernel.Process32FirstW(snapshot,ctypes.byref(entry))
        while valid:
            name=entry.szExeFile
            if name.lower() in {'guitar hero 2.exe','gh2_pc.exe','guitar hero 2 launcher.exe','gh2 launcher.exe'}:
                handle=kernel.OpenProcess(0x1000,False,entry.th32ProcessID)
                try:
                    path=ctypes.create_unicode_buffer(32768);size=w.DWORD(len(path))
                    if not handle or not kernel.QueryFullProcessImageNameW(handle,0,path,ctypes.byref(size)): raise RuntimeError('Cannot check '+name+'. Close it normally before updating.')
                    yield name,Path(path.value)
                finally:
                    if handle: kernel.CloseHandle(handle)
            valid=kernel.Process32NextW(snapshot,ctypes.byref(entry))
    finally: kernel.CloseHandle(snapshot)

def check_idle(root,processes=None):
    root=Path(root).resolve()
    for name,path in windows_processes() if processes is None else processes:
        if Path(path).resolve().is_relative_to(root): raise RuntimeError('Close '+name+' normally before installing an update.')

def installed_version(root):
    try: return json.loads((root/'build/updater/installed-release.json').read_text())['tag']
    except (OSError,KeyError,ValueError): pass
    if os.name=='nt':
        from ctypes import wintypes as w
        version=ctypes.WinDLL('version',use_last_error=True)
        version.GetFileVersionInfoSizeW.argtypes=[w.LPCWSTR,ctypes.POINTER(w.DWORD)]
        version.GetFileVersionInfoW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,ctypes.c_void_p]
        version.VerQueryValueW.argtypes=[ctypes.c_void_p,w.LPCWSTR,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(w.UINT)]
        file=str(root/'Guitar Hero 2 Launcher.exe');unused=w.DWORD();size=version.GetFileVersionInfoSizeW(file,ctypes.byref(unused))
        if size:
            data=ctypes.create_string_buffer(size);pointer=ctypes.c_void_p();length=w.UINT()
            if version.GetFileVersionInfoW(file,0,size,data) and version.VerQueryValueW(data,'\\',ctypes.byref(pointer),ctypes.byref(length)) and length.value>=52:
                words=ctypes.cast(pointer,ctypes.POINTER(w.DWORD))
                return f'{words[2]>>16}.{words[2]&65535}.{words[3]>>16}'
    return 'Not recorded'

class InstallLock:
    def __init__(self,root): self.path=Path(root)/'build/updater/install.lock';self.handle=None
    def __enter__(self):
        from ctypes import wintypes as w
        self.path.parent.mkdir(parents=True,exist_ok=True)
        self.kernel=ctypes.WinDLL('kernel32',use_last_error=True)
        self.kernel.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,ctypes.c_void_p,w.DWORD,w.DWORD,w.HANDLE]
        self.kernel.CreateFileW.restype=w.HANDLE
        self.kernel.CloseHandle.argtypes=[w.HANDLE]
        self.handle=self.kernel.CreateFileW(str(self.path),0xC0000000,0,None,4,0x80,None)
        if self.handle==ctypes.c_void_p(-1).value: raise RuntimeError('Another update is running, or the update folder is not writable.')
        return self
    def __exit__(self,*args): self.kernel.CloseHandle(self.handle)

def run_update(root,archive,release,emit,log):
    root=Path(root).resolve()
    if not (root/'private/default.xex').is_file(): raise RuntimeError('No imported game dump was found. Use the first-build instructions in the README.')
    with InstallLock(root):
        check_idle(root)
        manifest=validate_archive(archive)
        with zipfile.ZipFile(archive) as z:
            incoming=json.loads(z.read('dependencies.lock.json'))
        installed=json.loads((root/'dependencies.lock.json').read_text(encoding='utf-8'))
        expected_game=incoming.get('game',{}).get('xex_sha256')
        if expected_game:
            with (root/'private/default.xex').open('rb') as game:
                digest=hashlib.file_digest(game,'sha256').hexdigest()
            if digest!=expected_game: raise RuntimeError('This release does not support the imported game executable. No source files were changed.')
        if incoming.get('repositories')!=installed.get('repositories'):
            raise RuntimeError('This version requires different build dependencies. Install it in a separate folder using the README instructions.')
        emit('building','Installing source and building. Keep this window open.')
        python=Path(sys.executable)
        if python.name.lower()=='pythonw.exe': python=python.with_name('python.exe')
        # Use the selected release's installer snapshot, including its allowlist.
        # An older installation must not apply a new release using stale helper code.
        installer=Path(archive).resolve().parent/'installer';(installer/'scripts').mkdir(parents=True,exist_ok=True)
        with zipfile.ZipFile(archive) as z:
            for name in ('scripts/update_source.py','scripts/build_project.py','dependencies.lock.json'):
                (installer/name).write_bytes(z.read(name))
        apply_code='import sys;from pathlib import Path;sys.path.insert(0,sys.argv[1]);import update_source;update_source.ROOT=Path(sys.argv[2]);update_source.apply_archive(Path(sys.argv[3]))'
        commands=[[str(python),'-c',apply_code,str(installer/'scripts'),str(root),str(Path(archive).resolve())],
                  ['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(root/'Build.ps1'),'-Python',str(python)]]
        env=dict(os.environ,PYTHONIOENCODING='utf-8')
        build_receipt=root/'build/build-receipt.json'
        previous_receipt_time=build_receipt.stat().st_mtime_ns if build_receipt.exists() else None
        with log.open('a',encoding='utf-8') as output:
            for command in commands:
                with subprocess.Popen(command,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',env=env,creationflags=subprocess.CREATE_NO_WINDOW) as process:
                    for line in process.stdout:
                        output.write(line);output.flush();emit('log',line)
                    if process.wait()!=0: raise RuntimeError('The update did not finish. Open the log for details; your Documents saves and settings are unchanged.')
        if not build_receipt.exists() or build_receipt.stat().st_mtime_ns==previous_receipt_time:
            raise RuntimeError('The build did not write a new completion receipt. Open the log for details.')
        receipt=dict(tag=release['tag_name'],source_id=manifest.get('source_id'),installed_at_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()))
        target=root/'build/updater/installed-release.json';temporary=target.with_suffix('.tmp')
        temporary.write_text(json.dumps(receipt,indent=2),encoding='utf-8');os.replace(temporary,target)
        emit('installed',release['tag_name'])

class Updater:
    def __init__(self,root,gui_test=False):
        import tkinter as tk
        from tkinter import ttk, messagebox
        self.tk=tk;self.ttk=ttk;self.messagebox=messagebox;self.root=Path(root).resolve()
        self.events=queue.Queue();self.cancel=threading.Event();self.busy=False;self.building=False;self.closing=False;self.releases=[];self.log=None
        self.window=tk.Tk();self.window.title('Guitar Hero 2 Updater');self.window.geometry('820x660');self.window.minsize(680,550);self.window.configure(background='white')
        style=ttk.Style();style.theme_use('clam');style.configure('TFrame',background='white');style.configure('TLabel',background='white');style.configure('TButton',padding=8)
        frame=ttk.Frame(self.window,padding=20);frame.pack(fill='both',expand=True)
        ttk.Label(frame,text='Guitar Hero 2 Updates',font=('Segoe UI',20,'bold')).pack(anchor='w')
        icon=self.root/'launcher/assets/gh2pc-original.ico'
        if icon.exists(): self.window.iconbitmap(str(icon))
        current=installed_version(self.root)
        self.current=ttk.Label(frame,text='Installed version: '+current);self.current.pack(anchor='w',pady=(5,15))
        row=ttk.Frame(frame);row.pack(fill='x')
        self.version=ttk.Combobox(row,state='readonly');self.version.pack(side='left',fill='x',expand=True);self.version.bind('<<ComboboxSelected>>',self.selection)
        self.refresh=ttk.Button(row,text='Check releases',command=self.check);self.refresh.pack(side='left',padx=(10,0))
        self.preview=tk.BooleanVar(value=False);self.preview_box=ttk.Checkbutton(frame,text='Show preview releases',variable=self.preview,command=self.check);self.preview_box.pack(anchor='w',pady=8)
        ttk.Label(frame,text='Release notes').pack(anchor='w')
        self.notes=tk.Text(frame,height=7,wrap='word',font=('Segoe UI',10),state='disabled',background='#fafafa');self.notes.pack(fill='x',pady=(4,12))
        ttk.Label(frame,text='Downloads the selected source package and rebuilds using your existing dump.\nClose the game and launcher before installing. Saves and settings are kept.',wraplength=750).pack(anchor='w')
        self.status=ttk.Label(frame,text='Ready');self.status.pack(anchor='w',pady=(10,4))
        self.progress=ttk.Progressbar(frame,maximum=100);self.progress.pack(fill='x')
        self.output=tk.Text(frame,height=8,wrap='word',font=('Consolas',9),state='disabled');self.output.pack(fill='both',expand=True,pady=10)
        actions=ttk.Frame(frame);actions.pack(fill='x')
        self.install=ttk.Button(actions,text='Install selected version',command=self.start_install,state='disabled');self.install.pack(side='left')
        self.cancel_button=ttk.Button(actions,text='Cancel download',command=self.cancel.set,state='disabled');self.cancel_button.pack(side='left',padx=10)
        self.open_log=ttk.Button(actions,text='Open log',command=lambda:os.startfile(self.log) if self.log and self.log.exists() else None);self.open_log.pack(side='right')
        self.window.protocol('WM_DELETE_WINDOW',self.close)
        self.window.after(100,self.poll)
        if not gui_test: self.window.after(150,self.check)

    def emit(self,kind,value): self.events.put((kind,value))
    def work(self,operation):
        self.busy=True;self.cancel.clear();self.refresh.config(state='disabled');self.install.config(state='disabled');self.version.config(state='disabled');self.preview_box.config(state='disabled')
        def worker():
            try: operation()
            except Cancelled as error: self.emit('status',str(error))
            except Exception as error: self.emit('error',str(error))
            finally: self.emit('done',None)
        threading.Thread(target=worker,daemon=True).start()
    def check(self):
        if self.busy:return
        preview=self.preview.get();self.status.config(text='Checking GitHub releases…')
        self.work(lambda:self.emit('catalog',catalog(preview)))
    def selection(self,event=None):
        index=self.version.current()
        if index<0:return
        self.notes.config(state='normal');self.notes.delete('1.0','end');self.notes.insert('end',self.releases[index].get('body','No release notes.'));self.notes.config(state='disabled')
    def start_install(self):
        if self.busy or self.version.current()<0:return
        try: check_idle(self.root)
        except Exception as error:self.messagebox.showerror('Close the game and launcher',str(error));return
        release=self.releases[self.version.current()]
        job=self.root/'build/updater'/('job-'+time.strftime('%Y%m%d-%H%M%S')+'-'+str(time.time_ns()))
        job.mkdir(parents=True);self.log=job/'update.log';self.log.write_text('Selected release: '+release['tag_name']+'\n',encoding='utf-8')
        self.status.config(text='Downloading '+release['tag_name']+'…');self.cancel_button.config(state='normal');self.progress['value']=0
        def operation():
            archive=download_release(release,job,self.emit,self.cancel)
            if self.cancel.is_set(): raise Cancelled('Download cancelled.')
            run_update(self.root,archive,release,self.emit,self.log)
        self.work(operation)
    def poll(self):
        while not self.events.empty():
            kind,value=self.events.get()
            if kind=='catalog':
                self.releases,latest=value
                self.version['values']=[r['tag_name']+(' (preview)' if r.get('prerelease') else '')+(' — latest' if r['tag_name']==latest else '') for r in self.releases]
                index=next((i for i,r in enumerate(self.releases) if r['tag_name']==latest),0);self.version.current(index);self.selection();self.status.config(text='Choose a version. Latest release selected by default.')
            elif kind=='progress': self.progress['value']=value
            elif kind=='building':
                self.building=True;self.cancel_button.config(state='disabled');self.status.config(text=value);self.progress.config(mode='indeterminate');self.progress.start(12)
            elif kind=='log':
                self.output.config(state='normal');self.output.insert('end',value)
                if int(self.output.index('end-1c').split('.')[0])>500:self.output.delete('1.0','100.0')
                self.output.see('end');self.output.config(state='disabled')
            elif kind=='installed':self.current.config(text='Installed version: '+value);self.status.config(text='Update complete. Open the launcher to play.')
            elif kind=='status':self.status.config(text=value)
            elif kind=='error':
                self.status.config(text='Update could not finish.')
                if self.log:
                    with self.log.open('a',encoding='utf-8') as log:log.write('\nERROR: '+value+'\n')
                self.messagebox.showerror('Guitar Hero 2 Updater',value)
            elif kind=='done':
                self.busy=False;self.building=False;self.progress.stop();self.progress.config(mode='determinate');self.refresh.config(state='normal');self.preview_box.config(state='normal');self.version.config(state='readonly');self.install.config(state='normal' if self.releases else 'disabled');self.cancel_button.config(state='disabled')
                if self.closing:self.window.destroy();return
        self.window.after(100,self.poll)
    def close(self):
        if self.building:self.messagebox.showinfo('Build in progress','Please keep the updater open until the build finishes.');return
        if self.busy:self.closing=True;self.cancel.set();self.status.config(text='Finishing the current request…');return
        self.window.destroy()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path,required=True);parser.add_argument('--gui-test',action='store_true');parser.add_argument('--check-only',action='store_true');parser.add_argument('--download-only',action='store_true')
    args=parser.parse_args()
    if args.check_only or args.download_only:
        releases,latest=catalog();selected=next((r for r in releases if r['tag_name']==latest),releases[0])
        print(json.dumps({'releases':[r['tag_name'] for r in releases],'default':selected['tag_name']}))
        if args.download_only:print(download_release(selected,args.root/'build/updater/download-test',lambda *a:None,threading.Event()))
        return
    if not (args.root/'Update.ps1').is_file():raise RuntimeError('Place the updater in the root of your existing GH2 project.')
    ui=Updater(args.root,args.gui_test)
    if args.gui_test:
        ui.window.withdraw();ui.window.update();assert str(ui.version['state'])=='readonly'
        ui.emit('catalog',([{'tag_name':'v0.2.0','body':'Other version'},{'tag_name':'v0.1.1','body':'Latest release notes'}],'v0.1.1'))
        ui.poll();assert ui.version.current()==1;assert 'Latest release notes' in ui.notes.get('1.0','end')
        ui.window.destroy();print('Updater GUI, version selection and release-note checks passed.');return
    ui.window.mainloop()

if __name__=='__main__':
    try: main()
    except Exception as error:
        if any(flag in sys.argv for flag in ('--gui-test','--check-only','--download-only')):
            print('UPDATER ERROR: '+str(error),file=sys.stderr);sys.exit(1)
        if os.name=='nt':ctypes.windll.user32.MessageBoxW(None,str(error),'Guitar Hero 2 Updater',0x10)
        else:raise
