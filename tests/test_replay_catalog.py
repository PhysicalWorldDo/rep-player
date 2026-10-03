import json
import os
from pathlib import Path
import subprocess
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get('REP_CATALOG_EXE', ROOT / 'build' / 'rep_catalog.exe'))


class ReplayCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.client = ROOT / 'validation' / 'replay_browser' / ('catalog_' + uuid.uuid4().hex)
        cls.replay = cls.client / 'Replay'
        cls.relative_paths = (
            'SkillReplay/Swordman/BloodyRave.rep',
            'SkillReplay/Swordman/BloodyRave_VP1.rep',
            'SkillReplay/Swordman/Unknown.REP',
            'Dungeon/Swordman/BloodyRave.rep',
            'Dungeon/Nested/演示.REP',
            '剧情/第一幕/Opening.rep',
            'RootReplay.rep',
        )
        for relative in cls.relative_paths:
            path = cls.replay / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'')
        (cls.replay / 'Dungeon' / 'readme.txt').write_text('not a REP', encoding='utf8')

    def catalog(self, query='', root=None):
        self.assertTrue(EXE.is_file(), 'native catalog is missing')
        args = [str(EXE), str(self.client), query]
        if root is not None:
            args = [str(EXE), '--scan-root', str(root), query]
        result = subprocess.run(args, capture_output=True, text=True, encoding='utf8')
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_scans_every_rep_in_replay_and_preserves_relative_identity(self):
        data = self.catalog()
        self.assertEqual(data['total'], len(self.relative_paths))
        self.assertEqual({row['relative_path'] for row in data['items']}, set(self.relative_paths))
        root_item = next(row for row in data['items'] if row['file'] == 'RootReplay.rep')
        self.assertEqual(root_item['directories'], [])
        self.assertEqual(root_item['path'], str(self.replay / 'RootReplay.rep'))

    def test_directory_hierarchy_and_case_insensitive_path_search(self):
        data = self.catalog(query='dUnGeOn/Nested')
        self.assertEqual(len(data['items']), 1)
        self.assertEqual(data['items'][0]['file'], '演示.REP')
        self.assertEqual(data['items'][0]['directories'], ['Dungeon', 'Nested'])
        chinese = self.catalog(query='剧情/第一幕')['items']
        self.assertEqual(len(chinese), 1)
        self.assertEqual(chinese[0]['directories'], ['剧情', '第一幕'])

    def test_only_skill_replay_branch_uses_skill_translations(self):
        data = self.catalog()
        self.assertEqual(data['matched'], 2)
        by_path = {row.get('relative_path', ''): row for row in data['items']}
        self.assertIn('SkillReplay/Swordman/BloodyRave.rep', by_path)
        self.assertEqual(by_path['SkillReplay/Swordman/BloodyRave.rep']['zh'], '嗜魂封魔斩')
        self.assertEqual(by_path['SkillReplay/Swordman/BloodyRave_VP1.rep']['zh'], '嗜魂封魔斩 · VP1 · 猩红旋涡')
        self.assertEqual(by_path['Dungeon/Swordman/BloodyRave.rep']['zh'], 'BloodyRave.rep')
        self.assertEqual(by_path['Dungeon/Swordman/BloodyRave.rep']['job_zh'], '')
        self.assertEqual(by_path['SkillReplay/Swordman/Unknown.REP']['zh'], 'Unknown.REP')
        self.assertEqual({row.get('relative_path') for row in self.catalog(query='嗜魂封魔斩')['items']}, {
            'SkillReplay/Swordman/BloodyRave.rep', 'SkillReplay/Swordman/BloodyRave_VP1.rep',
        })

    def test_direct_skill_replay_scan_remains_compatible(self):
        data = self.catalog(root=self.replay / 'SkillReplay')
        self.assertEqual(data['total'], 3)
        self.assertEqual(data['matched'], 2)
        by_path = {row['relative_path']: row for row in data['items']}
        self.assertEqual(by_path['Swordman/BloodyRave.rep']['zh'], '嗜魂封魔斩')
        self.assertEqual(by_path['Swordman/BloodyRave.rep']['directories'], ['Swordman'])


if __name__ == '__main__':
    unittest.main()
