"""Reapply patches to pristine local Git objects in an isolated workspace."""
from pathlib import Path
import argparse,json,subprocess,sys,zipfile,shutil,hashlib
from build_project import ROOT,LOCK,source_files,safe_extract

def audit(git):
    destination=ROOT/'build/repro-audit/project';destination.mkdir(parents=True,exist_ok=True)
    for file in source_files():
        target=destination/file.relative_to(ROOT);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,target)
    reports=[]
    for repo in LOCK['repositories']:
        source=ROOT/'tools'/repo['directory'];target=destination/'tools'/repo['directory'];target.parent.mkdir(parents=True,exist_ok=True)
        revision=subprocess.check_output([git,'-C',str(source),'rev-parse','HEAD'],text=True).strip()
        if revision!=repo['revision']:raise RuntimeError('Local dependency is not the locked revision')
        archive=ROOT/'build/repro-audit'/f'{repo["directory"]}.zip'
        subprocess.run([git,'-C',str(source),'archive','--format=zip',f'--output={archive}','HEAD'],check=True)
        safe_extract(archive,target,materialize_links=True)
        for name in repo['submodules']:
            path=source/'thirdparty'/name;output=ROOT/'build/repro-audit'/f'{repo["directory"]}-{name}.zip'
            subprocess.run([git,'-C',str(path),'archive','--format=zip',f'--output={output}','HEAD'],check=True)
            safe_extract(output,target/'thirdparty'/name,materialize_links=True)
        reports.append(dict(repository=repo['directory'],revision=revision))
    for script in ('apply_tool_patches.py','apply_rexglue_patches.py'):
        for repeat in range(2):subprocess.run([sys.executable,'scripts/'+script],cwd=destination,check=True)
    differences=[]
    for repo in LOCK['repositories']:
        source=ROOT/'tools'/repo['directory'];target=destination/'tools'/repo['directory']
        tracked=subprocess.check_output([git,'-C',str(source),'ls-files'],text=True).splitlines()
        for relative in tracked:
            original=source/relative;fresh=target/relative
            if original.is_file() and fresh.is_file() and original.read_bytes().replace(b'\r\n',b'\n')!=fresh.read_bytes().replace(b'\r\n',b'\n'):differences.append(repo['directory']+'/'+relative)
        if repo['directory']=='rexglue-sdk':
            relative='thirdparty/FFmpeg/libavutil/internal.h'
            if (source/relative).read_bytes().replace(b'\r\n',b'\n')!=(target/relative).read_bytes().replace(b'\r\n',b'\n'):differences.append(repo['directory']+'/'+relative)
    report=dict(result='pass' if not differences else 'fail',repositories=reports,patches_idempotent=True,differences=differences,workspace=str(destination.relative_to(ROOT)),scope='Pristine pinned sources with required submodules exported from local Git objects; no game code or assets packaged.')
    (ROOT/'docs/reproducibility_audit.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
    if differences:raise RuntimeError('Local changes missing from reproducible patches: '+', '.join(differences))
    return destination

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--git',default='git');args=parser.parse_args();audit(args.git)
