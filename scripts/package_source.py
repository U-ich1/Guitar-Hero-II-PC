"""Create an allowlisted source-only release; exclude all game-derived binaries."""
from pathlib import Path
import json,zipfile
from build_project import ROOT,source_files,source_id,sha256

def package():
    revision=source_id();output=ROOT/'build/releases'/f'gh2-recomp-source-{revision[:12]}.zip';output.parent.mkdir(parents=True,exist_ok=True)
    files=source_files();manifest=dict(schema=1,source_id=revision,files={file.relative_to(ROOT).as_posix():sha256(file) for file in files})
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED) as archive:
        for file in files:archive.write(file,file.relative_to(ROOT).as_posix())
        archive.writestr('SOURCE-MANIFEST.json',json.dumps(manifest,indent=2))
    (output.with_suffix('.zip.sha256')).write_text(sha256(output)+'  '+output.name+'\n')
    print(output)
    print(f'{len(files)} source files, including original launcher artwork. No ISO, extracted game assets/icons, generated translations, binaries, saves, build outputs or downloaded tools included.')
    return output

if __name__=='__main__':package()
