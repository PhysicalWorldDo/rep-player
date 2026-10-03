"""Add the selected CN skill pages after build_skill_name_map.py.

Only the two registered page files and their exact REP directories are read.
Existing entries remain intact; repeating this command adds no duplicates.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

SELECTED_PAGES = (
    ('ImperialKnight', 'imperial knight', 'breaker', '帝国骑士/破浪者/skill-data.js'),
    ('ATPriest', 'at priest', 'atinfighter', '光职者(女)/蓝拳使者(女)/skill-data.js'),
)


def unique(values):
    return list(dict.fromkeys(value for value in values if value))


def supplement(name_map: Path, source_root: Path, rep_root: Path):
    data = json.loads(name_map.read_text('utf-8-sig'))
    existing = {entry['relativePath'].replace('\\', '/').casefold() for entry in data['entries']}
    additions = []
    coverage = []
    for job, base_job, profession_key, source in SELECTED_PAGES:
        text = (source_root / source).read_text('utf-8-sig')
        page = json.loads(text[text.index('{'):text.rindex('}') + 1])
        if (page['baseJob'], page['section']) != (base_job, profession_key):
            raise ValueError(f'{source}: unexpected profession registration')
        records = {}
        for field in ('skills', 'vpSkills'):
            for skill in page[field]:
                records.setdefault(skill['english'].casefold(), []).append(skill)
        matched = []
        added_paths = []
        for path in sorted((rep_root / job).iterdir(), key=lambda path: path.name.casefold()):
            if not path.is_file() or path.suffix.casefold() != '.rep':
                continue
            variant_match = re.fullmatch(r'(.+?)_(VP\d+)', path.stem, re.IGNORECASE)
            english, variant = ((variant_match[1], variant_match[2].upper())
                                if variant_match else (path.stem, 'base'))
            rows = records.get(english.casefold())
            if not rows:
                continue
            relative = f'{job}/{path.name}'
            matched.append(relative)
            if relative.casefold() in existing:
                continue
            names = unique(row['name'].strip() for row in rows if row.get('name'))
            options = unique(option['name'] for row in rows for option in row.get('vp', [])
                             if option['type'].upper() == variant)
            if len(names) != 1 or len(options) > 1:
                raise ValueError(f'{relative}: ambiguous source name or VP option')
            chinese = names[0]
            vp_name = options[0] if options else ''
            display = chinese
            if variant != 'base':
                display += ' · ' + variant
                if vp_name:
                    display += ' · ' + vp_name
            additions.append({
                'id': relative,
                'file': path.name,
                'path': str(path),
                'relativePath': relative,
                'job': job,
                'baseJob': base_job,
                'jobZh': page['classFamily'],
                'professions': [page['profession']],
                'professionKeys': [profession_key],
                'english': english,
                'variant': variant,
                'zh': chinese,
                'vpName': vp_name,
                'displayZh': display,
                'displayEn': path.stem,
                'matchStatus': 'exact',
                'chineseCandidates': [],
                'vpNameCandidates': [],
                'aliases': unique([path.name, path.stem, english, chinese,
                                   variant if variant != 'base' else '', vp_name,
                                   job, page['classFamily'], page['profession']]),
                'sources': [source],
                'sourceSkillIndices': unique(row['index'] for row in rows),
            })
            existing.add(relative.casefold())
            added_paths.append(relative)
        coverage.append({'job': job, 'source': source, 'matchedRepFiles': len(matched),
                         'addedRepFiles': len(added_paths), 'addedPaths': added_paths})
    if additions:
        data['entries'].extend(additions)
        counts = data['counts']
        counts['repFiles'] = len(data['entries'])
        for status in ('exact', 'ambiguous', 'unmatched'):
            counts[status] = sum(entry['matchStatus'] == status for entry in data['entries'])
        counts['vpFiles'] = sum(entry['variant'] != 'base' for entry in data['entries'])
        counts['vpNames'] = sum(bool(entry['vpName']) for entry in data['entries'])
        counts['missingVpNames'] = sum(entry['variant'] != 'base' and not entry['vpName']
                                      for entry in data['entries'])
        data.setdefault('supplements', []).append({
            'sourceRoot': str(source_root),
            'repRoot': str(rep_root),
            'pages': coverage,
            'addedRepFiles': len(additions),
        })
        name_map.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', 'utf-8')
    return {'addedRepFiles': len(additions), 'pages': coverage, 'counts': data['counts']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name-map', type=Path, default=Path(__file__).with_name('skill-name-map.json'))
    parser.add_argument('--source-root', type=Path,
                        default=Path(r'E:\DNFAutoPlay\DNFPVF\summary\全职业技能页复现'))
    parser.add_argument('--rep-root', type=Path,
                        default=Path(r'E:\WeGameApps\地下城与勇士：创新世纪\Replay\SkillReplay'))
    args = parser.parse_args()
    print(json.dumps(supplement(args.name_map, args.source_root, args.rep_root), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
