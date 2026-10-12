"""Produce fail-fast diagnostic stubs for the exact generated import list."""
from pathlib import Path
import re
names = re.findall(r'PPC_EXTERN_FUNC\((__imp__[^)]+)\);', Path('generated/ppc_recomp_shared.h').read_text())
out = ['#include "kernel_runtime.h"']
for name in sorted(set(names)):
    out.append(f'''PPC_FUNC({name}) {{
    gh2::kernel::import("{name.removeprefix('__imp__')}", ctx, base);
}}''')
Path('generated/gh2_import_stubs.cpp').write_text('\n'.join(out))
print(f'Prepared {len(set(names))} kernel import adapters; unsupported services still halt.')
