"""Summarize candidate passes; never infer verified motion vectors from constants."""
from pathlib import Path
from collections import Counter,defaultdict
import argparse,json

def analyze(path):
    frames=[]
    with Path(path).open(encoding='utf-8') as stream:
        header=json.loads(next(stream))
        if header.get('kind')!='gh2-temporal-input-candidates' or header.get('schema')!=1:
            raise ValueError('Unsupported temporal capture')
        for line in stream:
            if line.strip():frames.append(json.loads(line))
    passes=Counter();depths=Counter();constants=defaultdict(lambda:defaultdict(set))
    truncated=0;dimensions=Counter();constant_bases=set()
    for frame in frames:
        dimensions[tuple(frame['input'])]+=1
        truncated+=frame['seen_draws']>len(frame['draws'])
        for draw in frame['draws']:
            key=(draw['vs'],draw['ps'],draw['depth_info'],draw['depth_control'],draw['surface_info'])
            passes[key]+=1
            depths[tuple(draw['depth_resource'])]+=1
            constant_bases.add(draw['vs_constant_base'])
            for c in draw['constants']:constants[draw['vs']][c[0]].add(tuple(c[1:]))
    return dict(schema=1,capture_complete=header['complete'],frames=len(frames),truncated_frames=truncated,
        input_sizes=[dict(size=list(size),frames=count) for size,count in dimensions.items()],
        depth_candidates=[dict(description=list(desc),draws=count) for desc,count in depths.most_common()],
        candidate_passes=[dict(vs=f'{key[0]:016x}',ps=f'{key[1]:016x}',depth_info=key[2],depth_control=key[3],surface_info=key[4],draws=count) for key,count in passes.most_common()],
        varying_vertex_constants={f'{shader:016x}':[index for index,values in by_index.items() if len(values)>1] for shader,by_index in constants.items()},
        vertex_constant_bases=sorted(constant_bases),validated_dlss_inputs=False,
        limitations=['Target descriptions are metadata, not captured depth pixels.',
            'Varying constants can belong to different objects within the same frame; no stable object correspondence is established.',
            'No camera matrices, previous positions, motion vectors, jitter or HUD separation have been validated.',
            'Capture overhead means these frames are not a performance benchmark.'])

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('capture',type=Path);parser.add_argument('--output',type=Path)
    args=parser.parse_args();report=analyze(args.capture)
    output=args.output or args.capture.with_suffix('.summary.json')
    output.write_text(json.dumps(report,indent=2));print(output)
