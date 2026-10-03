import json
from pathlib import Path
root = Path(__file__).resolve().parents[1]
data = json.loads(Path(r'D:\DNF115us\skill_player\shader_static_presets.json').read_text(encoding='utf8'))
lines = ['#pragma once', 'namespace rep {']
for name in ('metal', 'shiny', 'glow'):
    rows = data[name]
    lines.append('inline constexpr float preset_%s[%d][%d] = {' % (name, len(rows), len(rows[0])))
    for row in rows:
        lines.append('{' + ','.join(format(float(x), '.9g') + 'f' if '.' in format(float(x), '.9g') else format(float(x), '.9g') + '.0f' for x in row) + '},')
    lines.append('};')
for name, row in list(data['outline'].items()) + [('dissolve', data['dissolve'])]:
    lines.append('inline constexpr float preset_%s[] = {%s};' % (name, ','.join(repr(float(x)) + 'f' for x in row)))
lines.append('}')
(root / 'src' / 'preset_table.hpp').write_text('\n'.join(lines) + '\n', encoding='utf8')
