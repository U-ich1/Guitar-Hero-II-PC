"""Bounded host process checks; reports distinguish GPU setup from guest progress."""
from pathlib import Path
import json
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
os.chdir(root)
windowed = '--windowed' in sys.argv
game = '--game' in sys.argv or windowed
name = 'gh2_pc' if windowed else 'gh2_rex_bootstrap' if game else 'gh2_rex_gpu_smoke'
scratch = root / 'build' / ('windowed-state' if windowed else 'rex-game-state' if game else 'rex-smoke-state')
scratch.mkdir(parents=True, exist_ok=True)
logfile = scratch / ('gh2.log' if game else 'gpu.log')
logfile.write_text('')
env = dict(os.environ)
env['PATH'] = str(root / 'tools/llvm-mingw-20261006-ucrt-x86_64/bin') + os.pathsep + env['PATH']
args = [root / f'tools/rexglue-sdk/out/win-amd64/{name}.exe', root / 'private', scratch]
limit = 45
if windowed:
    env['GH2_CAPTURE_FRAMES'] = '1'
    limit = int(next((arg.split('=', 1)[1] for arg in sys.argv if arg.startswith('--seconds=')), '95'))
    args = [args[0], '--mnk_mode=true', '--log_level=debug', '--log_file=' + str(logfile)]
    audio_offset = next((arg.split('=', 1)[1] for arg in sys.argv if arg.startswith('--audio-offset=')), None)
    if audio_offset is not None: args.append('--audio_offset_ms=' + audio_offset)
    if '--audio-panel' in sys.argv: env['GH2_OPEN_AUDIO_CALIBRATION'] = '1'
    if '--audio-reference' in sys.argv: env['GH2_AUDIO_REFERENCE_TEST'] = '1'
    fixture = next((arg.split('=', 1)[1] for arg in sys.argv if arg.startswith('--input=')), None)
    if fixture: env['GH2_TEST_INPUT'] = str((root / fixture).resolve())
timed_out = False
try:
    result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=limit)
    stdout, stderr, code = result.stdout, result.stderr, result.returncode
except subprocess.TimeoutExpired as error:
    stdout = error.stdout.decode(errors='replace') if isinstance(error.stdout, bytes) else (error.stdout or '')
    stderr = error.stderr.decode(errors='replace') if isinstance(error.stderr, bytes) else (error.stderr or '')
    code = None
    timed_out = True
text = logfile.read_text(errors='replace')
(root / f'build/{name}.log').write_text(stdout + stderr)
report = {
    'binary': name, 'exit_code': code, 'host_timeout': timed_out,
    'gpu_plugin_loaded': "GPU plugin 'xenos' loaded" in text,
    'd3d12_device_created': 'Direct3D 12 device and OS features:' in text,
    'gpu_runtime_initialized': 'GPU system initialized' in text,
    'guest_launched': 'Starting GH2 with the Xenos backend' in stdout or 'GH2 import ' in text and ' LR=' in text,
    'gpu_engine_import_reached': 'GH2 import VdInitializeEngines LR=' in text,
    'ring_buffer_import_reached': 'GH2 import VdInitializeRingBuffer LR=' in text,
    'physical_alias_checks_passed': 'SDK physical alias checks passed' in stdout,
    'gpu_batch_completed': 'GH2 diagnostic GPU batch completed:' in text,
    'gpu_command_words_nonzero': any(int(word, 16) != 0 for word in re.findall(r'first GPU word \d+: ([0-9A-F]{8})', text)),
    'presentation_enabled': 'presentation=true' in text,
    'captured_guest_frames': len(re.findall('GH2 guest frame captured:', text)),
    'audio_calibration_panel_drawn': 'GH2 audio calibration panel drawing' in text,
    'audio_reference_queued': 'GH2 flash/click reference audio queued' in text,
    'scope': 'Windowed diagnostic; inspect captured frames; song playability and timing remain unverified.' if windowed else 'Headless diagnostic only; no presented game frame, song playback or playability established.',
    'stdout': stdout, 'stderr': stderr,
}
(root / f'docs/{"windowed_startup" if windowed else "rex_startup" if game else "gpu_backend"}_validation.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
sys.exit(0 if code == 0 else 1)
