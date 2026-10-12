"""Use local pinned Git objects/tools as caches for an isolated full rebuild."""
from pathlib import Path
import argparse,os,shutil,subprocess,sys,json
from build_project import ROOT,LOCK,source_files

def prepare(git):
    destination=ROOT/'build/repro-audit/project'
    if not (destination/'tools/rexglue-sdk/CMakeLists.txt').exists():raise RuntimeError('Run the pristine patch audit first')
    # Refresh our own sources after workflow changes; never copy translations.
    for file in source_files():
        target=destination/file.relative_to(ROOT);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,target)
    for repo in LOCK['repositories']:
        paths=[('',ROOT/'tools'/repo['directory'],destination/'tools'/repo['directory'])]
        paths.extend((name,ROOT/'tools'/repo['directory']/'thirdparty'/name,destination/'tools'/repo['directory']/'thirdparty'/name) for name in repo['submodules'])
        for name,source,target in paths:
            if (target/'.git').exists():continue
            temporary=ROOT/'build/repro-audit/git-cache'/(repo['directory']+'-'+(name or 'root'))
            temporary.parent.mkdir(parents=True,exist_ok=True)
            subprocess.run([git,'clone','--shared','--no-checkout',str(source),str(temporary)],check=True)
            os.rename(temporary/'.git',target/'.git')
    for item in LOCK['archives']:
        archive=ROOT/'tools/downloads'/f'{item["name"]}.zip';target=destination/'tools/downloads'/archive.name;target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists():os.link(archive,target)
    private=destination/'private';private.mkdir(exist_ok=True)
    for source in (ROOT/'private').rglob('*'):
        if source.is_file() and (source.name=='default.xex' or source.relative_to(ROOT/'private').parts[0] in ('gen','videos')):
            target=private/source.relative_to(ROOT/'private');target.parent.mkdir(parents=True,exist_ok=True)
            if not target.exists():os.link(source,target)
    print(destination)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--git',default='git');prepare(parser.parse_args().git)
