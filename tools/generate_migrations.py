"""Build immutable native migration data from the preserved client evidence."""
import datetime
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
table = json.loads(Path(r'D:\DNF115us\skill_player\rep_resource_migration_rules.json').read_text(encoding='utf8'))
rows = []
def parts(value):
    if not value:
        return 0, 0
    t = datetime.datetime.fromisoformat(value)
    return t.year * 10000 + t.month * 100 + t.day, t.hour * 3600 + t.minute * 60 + t.second
for kind in ('legacy', 'country'):
    for rule in table[kind]:
        date, secs = parts(rule['release_time'])
        force, fs = parts(rule.get('force_record_time'))
        for source, target in rule['images'].items():
            rows.append('{%s,%d,%d,%d,%d,%d,%s,%s}' % (
                'true' if kind == 'legacy' else 'false', rule.get('country', 0), date, secs, force, fs,
                json.dumps(source), json.dumps(target)))
out = '#pragma once\nnamespace rep {\nstruct Migration { bool legacy; int country,date,seconds,forceDate,forceSeconds; const char *source,*target; };\n'
out += 'inline constexpr Migration migrationTable[] = {\n' + ',\n'.join(rows) + '\n};\n}\n'
(root / 'src' / 'migration_table.hpp').write_text(out, encoding='utf8')
print('Generated', len(rows), 'client migration entries')
