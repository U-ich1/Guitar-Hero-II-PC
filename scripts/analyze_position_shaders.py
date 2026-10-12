"""Identify directly expressed position projections in local disassemblies."""
from pathlib import Path
import argparse,json,re

def analyze(directory):
    shaders=[]
    for file in sorted(Path(directory).glob('*.vs.txt')):
        text=file.read_text();rows={}
        for mask,index in re.findall(r'\bdp4\s+oPos\.([xyzw_]+),\s+c(\d+)\.',text):
            lanes=mask.replace('_','')
            if len(lanes)==1:rows[lanes]=int(index)
        projection=[rows.get(lane) for lane in 'xyzw']
        shaders.append(dict(shader=file.stem.removesuffix('.vs'),
            direct_projection_rows=projection if all(row is not None for row in projection) else None,
            uses_object_constant_candidates=all(re.search(r'\bc'+str(index)+r'\.',text) for index in (24,25,26)),
            position_write_lines=[line.strip() for line in text.splitlines() if 'oPos' in line],
            validated_camera=False))
    return dict(vertex_shaders=len(shaders),direct_projection_shaders=sum(s['direct_projection_rows'] is not None for s in shaders),
        shaders=shaders,limitations=['Static microcode patterns do not establish camera ownership, object correspondence, skinning history or jitter placement.'])

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('directory',type=Path);parser.add_argument('--output',type=Path)
    args=parser.parse_args();report=analyze(args.directory)
    output=args.output or args.directory/'position-candidates.json';output.write_text(json.dumps(report,indent=2))
    print(f'{report["vertex_shaders"]} vertex shaders; {report["direct_projection_shaders"]} direct projection candidates. Report: {output}')
