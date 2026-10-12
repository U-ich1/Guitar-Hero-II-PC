"""Count supported position programs in an existing capture, not pixel coverage."""
from pathlib import Path
from collections import Counter
import argparse,json,re

def analyze(capture,root):
    lines=[json.loads(line) for line in capture.read_text().splitlines() if line.strip()]
    header,*frames=lines
    if header.get('kind')!='gh2-temporal-input-candidates' or header.get('schema')!=1:
        raise ValueError('Unsupported capture schema')
    source=(root/'runtime/linear_motion_clips.h').read_text()
    declaration=source.split('kLinearPositionShaders{',1)[1].split('};',1)[0]
    supported={int(value,16) for value in re.findall(r'0x([0-9a-fA-F]+)ULL',declaration)}
    supported.add(int(re.search(r'kLinearPositionShader=0x([0-9a-fA-F]+)',source)[1],16))
    skinned_declaration=source.split('kSkinnedPositionShaders{',1)[1].split('};',1)[0]
    supported.update(int(value,16) for value in re.findall(r'0x([0-9a-fA-F]+)ULL',skinned_declaration))
    counts=Counter(draw['vs'] for frame in frames for draw in frame['draws'])
    total=sum(counts.values());eligible=sum(n for shader,n in counts.items() if shader in supported)
    return dict(capture=capture.name,frames=len(frames),
        complete=bool(header.get('complete')) and all(f['seen_draws']==len(f['draws']) for f in frames),
        total_draw_calls=total,position_program_supported_draw_calls=eligible,
        shaders=[dict(shader=f'{shader:016x}',draw_calls=n,position_program_supported=shader in supported)
                 for shader,n in counts.most_common()],
        limitations=['Draw counts include tile replays and non-scene passes; they are not screen coverage.',
            'Shader support does not prove stable correspondence, eligible topology, visibility or native motion output.',
            'This analysis does not enable DLSS or measure frame rate.'])

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('capture',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();report=analyze(args.capture,Path(__file__).resolve().parents[1])
    args.output.write_text(json.dumps(report,indent=2))
    print(f"{report['position_program_supported_draw_calls']}/{report['total_draw_calls']} captured draw calls use supported position programs; not pixel coverage.")
