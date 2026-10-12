"""Summarize the latest GH2 launch without controlling or restarting the game."""
from pathlib import Path
import argparse,json,re,statistics

def summarize(text):
    # The launcher normally truncates its log. Also handle concatenated launches.
    marker=text.rfind('GH2 startup graphics:')
    if marker>=0:text=text[marker:]
    output=[];timings={};presentation=[];swaps=[];scene_samples=[];scene_state={}
    for line in text.splitlines():
        match=re.search(r'GH2 performance: game_output=([\d.]+)/s display_updates=([\d.]+)/s worst_interval=([\d.]+)ms cap=(\d+) display_vsync=(\w+) configured_scale=(\d+)x',line)
        if match:output.append(dict(output_fps=float(match[1]),display_updates=float(match[2]),worst_interval_ms=float(match[3]),cap=int(match[4]),vsync=match[5]=='true',configured_scale=int(match[6])))
        match=re.search(r'GH2 timing: (\w+) samples=(\d+) avg_ms=([\d.]+) max_ms=([\d.]+) total_ms=([\d.]+) window_s=([\d.]+) over16=(\d+) over33=(\d+)',line)
        if match:timings.setdefault(match[1],[]).append(dict(samples=int(match[2]),avg_ms=float(match[3]),max_ms=float(match[4]),total_ms=float(match[5]),window_s=float(match[6]),over16=int(match[7]),over33=int(match[8])))
        if 'GH2 presentation:' in line:presentation.append(line)
        scene=re.search(r'GH2 scene: no_audience=(true|false) crowd_draws_skipped=(\d+)',line)
        if scene:scene_state=dict(no_audience=scene[1]=='true',crowd_draws_skipped=int(scene[2]))
        scene=re.search(r'GH2 performance scene: status=(\d+) mode=(\S*) song=(\S*) venue=(\S*)',line)
        if scene and output:scene_samples.append(dict(output[-1],**scene_state,status=int(scene[1]),mode=scene[2],song=scene[3],venue=scene[4]))
        match=re.search(r'GH2 guest swap: samples=(\d+) avg_ms=([\d.]+) max_ms=([\d.]+)',line)
        if match:swaps.append(dict(samples=int(match[1]),avg_ms=float(match[2]),max_ms=float(match[3])))
    metrics={}
    for name,rows in timings.items():
        count=sum(row['samples'] for row in rows)
        metrics[name]=dict(samples=count,weighted_average_ms=round(sum(row['total_ms'] for row in rows)/count,3),maximum_ms=max(row['max_ms'] for row in rows),total_ms=sum(row['total_ms'] for row in rows),reported_window_s=round(sum(row['window_s'] for row in rows),3),over16=sum(row['over16'] for row in rows),over33=sum(row['over33'] for row in rows))
    return dict(output=dict(samples=len(output),average_fps=round(statistics.mean(row['output_fps'] for row in output),3) if output else None,lowest_sample_fps=min((row['output_fps'] for row in output),default=None),worst_interval_ms=max((row['worst_interval_ms'] for row in output),default=None)),timings=metrics,guest_swap=swaps,presentation=presentation,scene_samples=scene_samples,limits='Scene labels describe window-end state; a window may cross song, menu or crowd-toggle transitions. Submission GPU times are not whole-frame times. Sampled draw metrics measure only one in 64 calls and their totals must not be extrapolated as whole-frame cost. CPU scopes can overlap and must not be added together. Guest swap duration includes the SDK import and any waits, not total game logic. Display updates do not measure unique frames or monitor delivery. Configured scale may differ from active scale until restart.')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--log',type=Path,default=Path('build/windowed-state/gh2.log'));parser.add_argument('--output',type=Path,default=Path('build/performance-report.json'));args=parser.parse_args()
    report=summarize(args.log.read_text(errors='replace'));args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
