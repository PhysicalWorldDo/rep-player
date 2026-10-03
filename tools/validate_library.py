import json
from pathlib import Path
import subprocess
import time

root = Path(__file__).resolve().parents[1]
out = root / 'validation'
old = json.loads(Path(r'D:\DNF115us\skill_player\rep_universal_validation.json').read_text(encoding='utf8'))
records = old['records']
paths = out / 'registered_rep_paths.txt'
paths.write_text('\n'.join(r['path'] for r in records), encoding='utf8')
start = time.perf_counter()
with (out / 'native_library.jsonl').open('w', encoding='utf8') as stream:
    result = subprocess.run([str(root / 'build' / 'rep_validate.exe'), '--batch', str(paths)], stdout=stream, stderr=subprocess.PIPE)
actual = [json.loads(line) for line in (out / 'native_library.jsonl').read_text(encoding='utf8').splitlines()]
assert len(actual) == len(records), (len(actual), len(records), result.stderr)
failures = []
for expected, got in zip(records, actual):
    for native, oracle in [('version', 'version'), ('minor', 'minor'), ('command_count', 'command_count'), ('scenes', 'scenes'),
                           ('references', 'total_command_references'), ('aux_bytes', 'aux_bytes'), ('opcode_counts', 'opcode_counts'),
                           ('exact_eof', 'exact_eof'), ('resource_migrations', 'resource_migration_count')]:
        if got.get(native) != expected.get(oracle):
            failures.append({'path': expected['path'], 'field': native, 'expected': expected.get(oracle), 'actual': got.get(native), 'error': got.get('error')})
report = {'files': len(actual), 'exact_eof': sum(x.get('exact_eof', False) for x in actual),
          'scenes': sum(x.get('scenes', 0) for x in actual), 'references': sum(x.get('references', 0) for x in actual),
          'aux_bytes': sum(x.get('aux_bytes', 0) for x in actual), 'seconds': time.perf_counter() - start,
          'fields_compared_per_file': 9, 'failures': failures}
(out / 'protocol_library_differential.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(report if not failures else {**report, 'failures': failures[:10]}, indent=2))
raise SystemExit(bool(failures))
