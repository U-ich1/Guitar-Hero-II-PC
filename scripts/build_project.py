"""Pinned Windows setup/build orchestration. Never distributes or launches GH2."""
from pathlib import Path, PurePosixPath
import argparse,hashlib,json,os,platform,shutil,subprocess,sys,time,urllib.request,zipfile

ROOT=Path(__file__).resolve().parents[1]
LOCK=json.loads((ROOT/'dependencies.lock.json').read_text())
SOURCE_DIRS=('runtime','launcher','patches','scripts','tests','config')
SOURCE_ARTWORK=('launcher/assets/gh2pc-original-logo.png','launcher/assets/gh2pc-original-icon.png','launcher/assets/gh2pc-original.ico')
SOURCE_FILES=('Build.ps1','Update.ps1','CMakeLists.txt','README.md','Start-GH2.cmd','Start-GH2.ps1','dependencies.lock.json','requirements-build.txt','.gitignore')

def sha256(file):
    digest=hashlib.sha256()
    with Path(file).open('rb') as stream:
        for chunk in iter(lambda:stream.read(8*1024*1024),b''):digest.update(chunk)
    return digest.hexdigest()

def run(args,**kwargs):
    print('+ '+' '.join(map(str,args)),flush=True)
    subprocess.run(list(map(str,args)),cwd=ROOT,check=True,**kwargs)

def git_output(git,*args):
    return subprocess.check_output([git,*map(str,args)],cwd=ROOT,text=True,encoding='utf-8').strip()

def source_files():
    paths=[ROOT/name for name in SOURCE_FILES if (ROOT/name).is_file()]
    paths.extend(ROOT/name for name in SOURCE_ARTWORK if (ROOT/name).is_file())
    for directory in SOURCE_DIRS:
        for path in (ROOT/directory).rglob('*'):
            if path.is_file() and path.suffix.lower() in ('.py','.ps1','.cpp','.h','.inl','.toml','.md','.rc') and '__pycache__' not in path.parts:
                paths.append(path)
    paths.extend((ROOT/'docs').glob('*.md'))
    return sorted(set(paths))

def source_id():
    digest=hashlib.sha256()
    for file in source_files():digest.update(file.relative_to(ROOT).as_posix().encode());digest.update(file.read_bytes() if file.suffix in ('.png','.ico') else file.read_bytes().replace(b'\r\n',b'\n'))
    return digest.hexdigest()

def safe_extract(archive,destination,materialize_links=False):
    destination=Path(destination).resolve()
    with zipfile.ZipFile(archive) as package:
        for item in package.infolist():
            relative=PurePosixPath(item.filename.replace('\\','/'))
            target=destination.joinpath(*relative.parts).resolve()
            if relative.is_absolute() or '..' in relative.parts or ':' in item.filename or not target.is_relative_to(destination):
                raise RuntimeError('Unsafe archive member: '+item.filename)
            if (item.external_attr>>16)&0o170000==0o120000 and not materialize_links:raise RuntimeError('Archive symlinks are not supported')
        package.extractall(destination)

def setup_archive(item,offline):
    archive=ROOT/'tools/downloads'/f'{item["name"]}.zip'
    destination=ROOT/'tools'/item['directory']
    archive.parent.mkdir(parents=True,exist_ok=True)
    if not archive.exists():
        if offline:raise RuntimeError('Offline archive missing: '+str(archive))
        partial=archive.with_suffix('.partial')
        print('Downloading '+item['name'],flush=True)
        with urllib.request.urlopen(item['url'],timeout=120) as response,partial.open('wb') as output:shutil.copyfileobj(response,output)
        if sha256(partial)!=item['sha256']:raise RuntimeError('Downloaded archive checksum mismatch: '+item['name'])
        partial.replace(archive)
    if sha256(archive)!=item['sha256']:raise RuntimeError('Cached archive checksum mismatch: '+str(archive))
    if not (destination/item['probe']).is_file():safe_extract(archive,ROOT/'tools')
    if not (destination/item['probe']).is_file():raise RuntimeError('Archive layout differs from the pinned tool: '+item['name'])

def setup_repo(item,git,offline):
    path=ROOT/'tools'/item['directory']
    if not (path/'.git').exists():
        if path.exists() and any(path.iterdir()):raise RuntimeError('Dependency directory is not a Git checkout: '+str(path))
        if offline:raise RuntimeError('Offline dependency missing: '+str(path))
        run([git,'init',path]);run([git,'-C',path,'remote','add','origin',item['url']])
        run([git,'-C',path,'fetch','--depth','1','origin',item['revision']]);run([git,'-C',path,'checkout','--detach','FETCH_HEAD'])
    actual=git_output(git,'-C',path,'rev-parse','HEAD')
    if actual!=item['revision']:
        raise RuntimeError(f'{item["directory"]} is at {actual}, expected {item["revision"]}. Preserve your changes and move this vendor checkout aside before rerunning setup; it will not be reset automatically.')
    modules=[]
    for module in item['submodules']:
        relative='thirdparty/'+module
        row=git_output(git,'-C',path,'ls-tree','HEAD',relative).split()
        if len(row)<3 or row[0]!='160000':raise RuntimeError('Submodule pin not found: '+relative)
        module_path=path/relative
        initialized=(module_path/'.git').exists()
        if initialized:
            if git_output(git,'-C',module_path,'rev-parse','HEAD')!=row[2]:raise RuntimeError('Submodule revision differs from its gitlink: '+relative)
        else:modules.append(relative)
    if modules:
        if offline:raise RuntimeError('Offline submodules missing: '+', '.join(modules))
        run([git,'-C',path,'submodule','update','--init','--depth','1','--',*modules])
    return actual

def validate_dump(directory):
    directory=Path(directory)
    xex=directory/'default.xex'
    if not xex.is_file():raise RuntimeError('Dump is missing default.xex')
    actual=sha256(xex)
    if actual!=LOCK['game']['xex_sha256']:raise RuntimeError('Unsupported GH2 executable revision. SHA256: '+actual+'\nExpected: '+LOCK['game']['xex_sha256'])
    for relative in ('gen/main.hdr','gen/main_0.ark','videos/intro.wmv'):
        if not (directory/relative).is_file():raise RuntimeError('Incomplete extracted dump: missing '+relative)
    return actual

def import_dump(source,python):
    private=ROOT/'private'
    if source is None:return validate_dump(private)
    source=Path(source).expanduser().resolve()
    if source.is_dir():
        validate_dump(source)
        if source!=private.resolve():
            private.mkdir(exist_ok=True)
            for relative in ('default.xex','gen','videos'):
                origin=source/relative;destination=private/relative
                if origin.is_dir():
                    for item in origin.rglob('*'):
                        if item.is_symlink():raise RuntimeError('Dump symlinks are not supported')
                    shutil.copytree(origin,destination,dirs_exist_ok=True)
                else:shutil.copy2(origin,destination)
    elif source.is_file():
        # Inspect/extract the executable into a temporary workspace first,
        # rejecting an unsupported revision before importing the full dump.
        probe=ROOT/'build/dump-probe';probe.mkdir(parents=True,exist_ok=True)
        if (probe/'default.xex').exists():(probe/'default.xex').unlink()
        run([python,'scripts/inspect_disc.py',source,'--output',probe])
        if sha256(probe/'default.xex')!=LOCK['game']['xex_sha256']:raise RuntimeError('ISO contains an unsupported executable revision')
        run([python,'scripts/inspect_disc.py',source,'--output',private,'--all'])
    else:raise RuntimeError('Dump path does not exist: '+str(source))
    return validate_dump(private)

def setup_python(offline):
    environment=ROOT/'tools/python-env';python=environment/'Scripts/python.exe'
    if not python.exists():run([sys.executable,'-m','venv',environment])
    expected=dict(line.split('==') for line in (ROOT/'requirements-build.txt').read_text().splitlines() if line and not line.startswith('#'))
    query='from importlib.metadata import version; import json; print(json.dumps({p:version(p) for p in '+repr(list(expected))+'}))'
    probe=subprocess.run([str(python),'-c',query],capture_output=True,text=True)
    installed=json.loads(probe.stdout) if probe.returncode==0 else {}
    if installed!=expected:
        if offline:raise RuntimeError('Offline Python environment does not contain the pinned packages. Run online setup once.')
        run([python,'-m','pip','install','--only-binary=:all:','-r','requirements-build.txt'])
    return python

def build(args):
    if os.name!='nt' or platform.machine().lower() not in ('amd64','x86_64'):raise RuntimeError('This build workflow currently supports Windows x64 only')
    if sys.version_info<(3,11):raise RuntimeError('Python 3.11 or newer is required')
    git=shutil.which(args.git) or args.git
    git_output(git,'--version')
    if args.check:
        print(json.dumps(dict(source_id=source_id(),game=LOCK['game'],archives=LOCK['archives'],repositories=LOCK['repositories'],existing_dump_valid=validate_dump(ROOT/'private') if (ROOT/'private/default.xex').exists() else None),indent=2));return
    (ROOT/'build').mkdir(exist_ok=True)
    for item in LOCK['archives']:setup_archive(item,args.offline)
    for item in LOCK['repositories']:setup_repo(item,git,args.offline)
    python=setup_python(args.offline)
    if args.launcher_only:
        validate_dump(ROOT/'private')
        if not (ROOT/'private/default.exe').exists():raise RuntimeError('A full build is needed before a launcher-only update')
    else:
        import_dump(args.dump,python)
        run([python,'scripts/inspect_xex.py']);run([python,'scripts/analyze_image.py'])
        if sha256(ROOT/'private/default.exe')!=LOCK['game']['image_sha256']:raise RuntimeError('Decoded image does not match the supported game')
        run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File','scripts/build_tools.ps1','-Python',python])
        run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File','scripts/recompile.ps1','-Python',python])
        run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File','scripts/build_gpu_backend.ps1','-Windowed','-Jobs',args.jobs,'-Python',python])
        run([python,'scripts/prepare_pc_menu.py'])
    run([python,'scripts/extract_launcher_icon.py'])
    run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File','scripts/build_launcher.ps1'])
    output=ROOT/'tools/rexglue-sdk/out/win-amd64'
    receipt=dict(schema=1,source_id=source_id(),source_id_algorithm='sha256-path-and-LF-normalized-source',built_at_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),lock_sha256=sha256(ROOT/'dependencies.lock.json'),
                 game=LOCK['game'],dependencies=LOCK['repositories'],artifacts={})
    for file in (ROOT/'Guitar Hero 2 Launcher.exe',output/'Guitar Hero 2.exe',output/'librexruntime.dll',output/'rexgpu-xenos.dll'):
        if file.exists():receipt['artifacts'][file.relative_to(ROOT).as_posix()]=sha256(file)
    (ROOT/'build/build-receipt.json').write_text(json.dumps(receipt,indent=2))
    print('Build complete. Open Guitar Hero 2 Launcher.exe. Saves and settings in Documents were not modified.')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--dump');parser.add_argument('--git',default='git');parser.add_argument('--jobs',type=int,default=4)
    parser.add_argument('--check',action='store_true');parser.add_argument('--offline',action='store_true');parser.add_argument('--launcher-only',action='store_true')
    try:build(parser.parse_args())
    except (RuntimeError,subprocess.CalledProcessError,OSError) as error:print('BUILD ERROR: '+str(error),file=sys.stderr);sys.exit(1)
