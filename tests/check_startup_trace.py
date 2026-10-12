"""Verify native archive-worker startup; this is not a playability test."""
import json, re, subprocess
from pathlib import Path
process = subprocess.run(['build/gh2/gh2_bootstrap.exe', '--trace-entry', 'private/default.exe'], capture_output=True, text=True, timeout=30)
trace = process.stdout + process.stderr
required = ['Loader ready:', 'Created guest worker 2', 'Kernel XGetVideoMode', 'main.hdr', 'main_0.ark', 'Physical allocation:', 'Kernel MmQueryAllocationSize', 'Unimplemented Xbox import VdInitializeEngines']
if process.returncode != 2 or any(marker not in trace for marker in required) or 'Worker stopped:' in trace:
    raise AssertionError(f'Unexpected startup result ({process.returncode}): {trace}')
count = re.search(r'Mapped (\d+) native functions', trace)
read = re.search(r'Game file bytes read: (\d+)', trace)
if not count or not read or int(read.group(1)) < 1800000:
    raise AssertionError('Native dispatch or worker archive reads missing')
report = {'native_build': 'pass', 'entry_execution': 'pass', 'archive_worker': 'pass', 'game_boot': 'not achieved', 'mapped_native_functions': int(count.group(1)), 'game_bytes_read': int(read.group(1)), 'diagnostic_exit': process.returncode, 'physical_memory': 'pass', 'first_runtime_blocker': 'VdInitializeEngines', 'trace': trace}
Path('docs/startup_validation.json').write_text(json.dumps(report, indent=2))
Path('build/startup_trace.log').write_text(trace)
print('Native archive worker and video-mode query passed; physical allocation passed; GPU engine backend remains the next blocker.')
