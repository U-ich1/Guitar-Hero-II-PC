"""Apply a verified source archive with backups; never touch dumps or saves."""
from pathlib import Path,PurePosixPath
import argparse,hashlib,json,os,time,zipfile
from build_project import ROOT,SOURCE_DIRS,SOURCE_FILES

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
            allowed=name in SOURCE_FILES or name=='launcher/assets/gh2-pc-logo.png' or (relative.parts[0] in SOURCE_DIRS and relative.suffix.lower() in ('.py','.ps1','.cpp','.h','.inl','.toml','.md','.rc')) or (relative.parts[0]=='docs' and relative.suffix=='.md')
            target=ROOT.joinpath(*relative.parts)
            if not allowed or not target.resolve().is_relative_to(ROOT.resolve()):raise RuntimeError('Archive targets a non-source file: '+name)
            data=package.read(name)
            if hashlib.sha256(data).hexdigest()!=checksum:raise RuntimeError('Source checksum mismatch: '+name)
            pending.append((target,data))
        changed=[]
        try:
            for target,data in pending:
                original=target.read_bytes() if target.exists() else None
                if original==data:continue
                if original is not None:
                    saved=backup/target.relative_to(ROOT);saved.parent.mkdir(parents=True,exist_ok=True);saved.write_bytes(original)
                target.parent.mkdir(parents=True,exist_ok=True);temporary=target.with_name(target.name+'.update.tmp');temporary.write_bytes(data);os.replace(temporary,target)
                changed.append((target,original))
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
