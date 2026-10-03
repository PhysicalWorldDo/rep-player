import json
import os
from pathlib import Path
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
CLIENT = Path(r'D:\115us\client')

class NativeCatalogTests(unittest.TestCase):
    def catalog(self, client=CLIENT, query=''):
        exe = Path(os.environ.get('REP_CATALOG_EXE', ROOT / 'build' / 'rep_catalog.exe'))
        self.assertTrue(exe.is_file(), 'native bilingual catalog is missing')
        result = subprocess.run([str(exe), str(client), query], capture_output=True, text=True, encoding='utf8')
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_exact_catalog_and_language_independent_alias_search(self):
        data = self.catalog(query='嗜魂封魔斩')
        expected_total = sum(1 for path in (CLIENT / 'Replay').rglob('*')
                             if path.is_file() and path.suffix.lower() == '.rep')
        self.assertEqual(data['total'], expected_total)
        self.assertEqual(data['matched'], 2878)
        self.assertEqual({x['file'] for x in data['items']}, {'BloodyRave.rep', 'BloodyRave_VP1.rep', 'BloodyRave_VP2.rep'})
        self.assertEqual({x['file'] for x in self.catalog(query='bLoOdYrAvE')['items']}, {x['file'] for x in data['items']})
        self.assertEqual(self.catalog(query='猩红旋涡')['items'][0]['file'], 'BloodyRave_VP1.rep')
        self.assertEqual(self.catalog(query='沉寂修罗')['items'][0]['file'], 'BloodyRave_VP2.rep')
        self.assertEqual(self.catalog(query='祈愿·天使赞歌')['items'][0]['file'], 'LausDeAngelus.rep')

    def test_new_client_matches_relative_job_and_keeps_unknown_filename(self):
        client = ROOT / 'validation' / 'replay_browser' / ('catalog_client_' + uuid.uuid4().hex)
        replays = client / 'Replay' / 'SkillReplay' / 'Swordman'
        replays.mkdir(parents=True, exist_ok=True)
        for name in ('BloodyRave.rep', 'NewUnregisteredSkill.rep'):
            (replays / name).write_bytes(b'')
        data = self.catalog(client)
        self.assertEqual(data['total'], 2)
        self.assertEqual(data['matched'], 1)
        by_file = {x['file']: x for x in data['items']}
        self.assertEqual(by_file['BloodyRave.rep']['zh'], '嗜魂封魔斩')
        self.assertEqual(by_file['NewUnregisteredSkill.rep']['zh'], 'NewUnregisteredSkill.rep')
        self.assertEqual(by_file['BloodyRave.rep']['path'], str(replays / 'BloodyRave.rep'))

if __name__ == '__main__':
    unittest.main()
