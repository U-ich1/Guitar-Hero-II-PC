"""Apply a verified source archive with backups; never touch dumps or saves."""
from pathlib import Path,PurePosixPath
import argparse,hashlib,json,os,time,zipfile
from build_project import ROOT,SOURCE_DIRS,SOURCE_FILES,SOURCE_ARTWORK

def source_path(name):
    relative=PurePosixPath(name)
    if not relative.parts or relative.is_absolute() or '..' in relative.parts or '\\' in name or ':' in name:return False
    return name in SOURCE_FILES or name in SOURCE_ARTWORK or (relative.parts[0] in SOURCE_DIRS and relative.suffix.lower() in ('.py','.ps1','.cpp','.h','.inl','.toml','.md','.rc')) or (relative.parts[0]=='docs' and relative.suffix=='.md')

def apply_archive(archive):
    backup=ROOT/'build/source-update-backups'/time.strftime('%Y%m%d-%H%M%S')
    with zipfile.ZipFile(archive) as package:
        manifest=json.loads(package.read('SOURCE-MANIFEST.json'))
        if manifest.get('schema')!=1:raise RuntimeError('Unsupported source archive manifest')
        expected=set(manifest['files'])|{'SOURCE-MANIFEST.json'}
        if set(package.namelist())!=expected or len(package.namelist())!=len(expected):raise RuntimeError('Source archive contains unlisted or duplicate files')
        pending=[]
        for name,checksum in manifest['files'].items():
            relative=PurePosixPath(name)
            if relative.is_absolute() or '..' in relative.parts or '\\' in name or ':' in name:raise RuntimeError('Unsafe source path: '+name)
            allowed=source_path(name)
            target=ROOT.joinpath(*relative.parts)
            if not allowed or not target.resolve().is_relative_to(ROOT.resolve()):raise RuntimeError('Archive targets a non-source file: '+name)
            data=package.read(name)
            if hashlib.sha256(data).hexdigest()!=checksum:raise RuntimeError('Source checksum mismatch: '+name)
            pending.append((target,data))
        obsolete=[]
        previous={}
        for saved_manifest in (ROOT/'build/source-installed-manifest.json',ROOT/'SOURCE-MANIFEST.json'):
            if saved_manifest.is_file():
                previous=json.loads(saved_manifest.read_text(encoding='utf-8'));break
        if previous.get('schema')==1:
            for name,checksum in previous.get('files',{}).items():
                target=ROOT.joinpath(*PurePosixPath(name).parts)
                if name not in manifest['files'] and source_path(name) and target.resolve().is_relative_to(ROOT.resolve()) and target.is_file():
                    original=target.read_bytes()
                    # Keep edited or unrelated files; retire only unchanged tracked sources.
                    if hashlib.sha256(original).hexdigest()==checksum:obsolete.append((target,original))
        changed=[]
        try:
            for target,data in pending:
                original=target.read_bytes() if target.exists() else None
                if original==data:continue
                if original is not None:
                    saved=backup/target.relative_to(ROOT);saved.parent.mkdir(parents=True,exist_ok=True);saved.write_bytes(original)
                target.parent.mkdir(parents=True,exist_ok=True);temporary=target.with_name(target.name+'.update.tmp');temporary.write_bytes(data);os.replace(temporary,target)
                changed.append((target,original))
            for target,original in obsolete:
                saved=backup/target.relative_to(ROOT);saved.parent.mkdir(parents=True,exist_ok=True);saved.write_bytes(original)
                target.unlink();changed.append((target,original))
        except Exception:
            for target,original in reversed(changed):
                if original is None:target.unlink()
                else:target.write_bytes(original)
            raise
    (ROOT/'build').mkdir(exist_ok=True)
    (ROOT/'build/source-installed-manifest.json').write_text(json.dumps(manifest,indent=2))
    print(f'Applied source {manifest["source_id"][:12]}. Changed files: {len(changed)}. Previous copies: {backup}')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('archive',type=Path);args=parser.parse_args()
    try:apply_archive(args.archive)
    except (RuntimeError,OSError,zipfile.BadZipFile,KeyError) as error:raise SystemExit('UPDATE ERROR: '+str(error))
