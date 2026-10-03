"""Check the two selected skill pages against the real CN REP filenames."""
import json
import os
from pathlib import Path
import re
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = Path(r'E:\DNFAutoPlay\DNFPVF\summary\全职业技能页复现')
CLIENT = Path(r'E:\WeGameApps\地下城与勇士：创新世纪')
NAME_MAP = ROOT / 'ui_design' / 'data' / 'skill-name-map.json'
OUTPUT = ROOT / 'validation' / 'skill_names_20261004'


def selected_replays(source, job):
    text = (SOURCE_ROOT / source).read_text('utf-8-sig')
    page = json.loads(text[text.index('{'):text.rindex('}') + 1])
    skills = {}
    for field in ('skills', 'vpSkills'):
        for skill in page[field]:
            skills.setdefault(skill['english'].casefold(), []).append(skill)
    expected = {}
    for path in (CLIENT / 'Replay' / 'SkillReplay' / job).iterdir():
        if not path.is_file() or path.suffix.casefold() != '.rep':
            continue
        variant = re.fullmatch(r'(.+?)_(VP\d+)', path.stem, re.IGNORECASE)
        english, vp = (variant[1], variant[2].upper()) if variant else (path.stem, 'base')
        records = skills.get(english.casefold())
        if records:
            names = {record['name'] for record in records}
            assert len(names) == 1, (path, names)
            options = {option['name'] for record in records for option in record.get('vp', [])
                       if option['type'].upper() == vp}
            assert len(options) <= 1, (path, options)
            expected[f'{job}/{path.name}'] = (names.pop(), options.pop() if options else '', vp)
    return page, expected


class NewSkillNameTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = json.loads(NAME_MAP.read_text('utf-8'))
        cls.by_path = {entry['relativePath']: entry for entry in cls.data['entries']}

    def check_source_names(self, source, job, count):
        page, expected = selected_replays(source, job)
        self.assertEqual(len(expected), count)
        self.assertEqual(sum(vp != 'base' for _, _, vp in expected.values()), 16)
        self.assertEqual(set(expected) - self.by_path.keys(), set(), 'selected REP names are missing')
        for relative, (chinese, option, vp) in expected.items():
            with self.subTest(rep=relative):
                entry = self.by_path[relative]
                self.assertEqual(entry['zh'], chinese)
                self.assertEqual(entry['vpName'], option)
                self.assertEqual(entry['variant'], vp)
                self.assertEqual(entry['jobZh'], page['classFamily'])
                self.assertIn(page['profession'], entry['professions'])
                self.assertIn(page['section'], entry['professionKeys'])
                self.assertIn(source, entry['sources'])
                self.assertEqual(entry['matchStatus'], 'exact')
                display = chinese if vp == 'base' else f'{chinese} · {vp} · {option}'
                self.assertEqual(entry['displayZh'], display)
                self.assertIn(chinese, entry['aliases'])
                if option:
                    self.assertIn(option, entry['aliases'])

    def test_imperial_knight_names_cover_all_44_replays(self):
        self.check_source_names('帝国骑士/破浪者/skill-data.js', 'ImperialKnight', 44)

    def test_female_infighter_names_cover_48_replays_including_common_skills(self):
        self.check_source_names('光职者(女)/蓝拳使者(女)/skill-data.js', 'ATPriest', 48)

    def catalog(self, client, query=''):
        exe = Path(os.environ.get('REP_CATALOG_EXE', ROOT / 'build' / 'rep_catalog.exe'))
        result = subprocess.run([str(exe), str(client), query], capture_output=True,
                                text=True, encoding='utf-8')
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_native_catalog_resolves_chinese_vp_and_english_search(self):
        client = OUTPUT / ('catalog_' + uuid.uuid4().hex)
        files = ('ImperialKnight/GaleSlash.rep', 'ImperialKnight/GaleSlash_VP1.rep',
                 'ImperialKnight/GaleSlash_VP2.rep', 'ATPriest/OneTwo.rep',
                 'ATPriest/StormFist_VP1.rep', 'ATPriest/ViolentCrush_VP2.rep')
        for relative in files:
            path = client / 'Replay' / 'SkillReplay' / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'')
        data = self.catalog(client)
        self.assertEqual((data['total'], data['matched']), (6, 6))
        self.assertEqual({row['file'] for row in self.catalog(client, '狂风斩')['items']},
                         {'GaleSlash.rep', 'GaleSlash_VP1.rep', 'GaleSlash_VP2.rep'})
        for query, filename in (('十字风斩', 'GaleSlash_VP1.rep'), ('追风之刃', 'GaleSlash_VP2.rep'),
                                ('双刺拳', 'OneTwo.rep'), ('狂怒风暴', 'StormFist_VP1.rep'),
                                ('震地余威', 'ViolentCrush_VP2.rep')):
            with self.subTest(query=query):
                self.assertEqual([row['file'] for row in self.catalog(client, query)['items']], [filename])
        self.assertEqual({row['file'] for row in self.catalog(client, 'gAlEsLaSh')['items']},
                         {'GaleSlash.rep', 'GaleSlash_VP1.rep', 'GaleSlash_VP2.rep'})

    def test_native_catalog_uses_relative_path_and_preserves_actual_casing(self):
        client = OUTPUT / ('casing_' + uuid.uuid4().hex)
        for relative in ('SkillReplay/imperialknight/gALeSlAsH.REP', 'Dungeon/ImperialKnight/GaleSlash.rep'):
            path = client / 'Replay' / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'')
        data = self.catalog(client)
        self.assertEqual((data['total'], data['matched']), (2, 1))
        by_path = {row['relative_path']: row for row in data['items']}
        skill = by_path['SkillReplay/imperialknight/gALeSlAsH.REP']
        self.assertEqual(skill['zh'], '狂风斩')
        self.assertEqual(skill['file'], 'gALeSlAsH.REP')
        self.assertEqual(by_path['Dungeon/ImperialKnight/GaleSlash.rep']['zh'], 'GaleSlash.rep')


if __name__ == '__main__':
    unittest.main()
