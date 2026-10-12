"""Turn the initial recompiler diagnostics into a compact report."""
import collections
import json
import re
from pathlib import Path

log = Path('build/recompile.log').read_text(encoding='utf-8')
unsupported = collections.Counter(re.findall(r'Unrecognized instruction at 0x[0-9A-Fa-f]+: (\S+)', log))
switch_errors = re.findall(r'ERROR: Switch case at ([0-9A-Fa-f]+) is trying to jump outside function: ([0-9A-Fa-f]+)', log)
mapping = Path('generated/ppc_func_mapping.cpp').read_text(encoding='utf-8')
output = list(Path('generated').glob('*'))
report = {
    'status': 'Translation emitted; see separate instruction and compiler validation reports. Full Xbox runtime not implemented.',
    'detected_switch_tables': Path('config/gh2_all_switch_tables.toml').read_text().count('[[switch]]'),
    'generated_files': len(output),
    'generated_bytes': sum(p.stat().st_size for p in output if p.is_file()),
    'unsupported_instruction_occurrences': sum(unsupported.values()),
    'unsupported_instruction_types': dict(unsupported.most_common()),
    'switch_boundary_error_count': len(switch_errors),
    'switch_boundary_error_sites': sorted({f'0x{site}' for site, _ in switch_errors}),
    'configuration_diagnostics': [line for line in log.splitlines() if line.startswith('ERROR: __')],
    'other_translation_diagnostics': [line for line in log.splitlines() if 'has RC bit' in line or 'no switch table entry' in line or 'Unable to decode' in line],
}
Path('docs/recomp_diagnostics.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps({k: v for k, v in report.items() if k != 'switch_boundary_error_sites'}, indent=2))
