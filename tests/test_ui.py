import json
from pathlib import Path
import subprocess
import unittest
import struct
import sys
sys.dont_write_bytecode=True
sys.path.insert(0,r'D:\DNF115us\skill_player')
from test_rep_protocol_versions import pack_replay
ROOT=Path(__file__).resolve().parents[1]
class NativeUiTests(unittest.TestCase):
    def test_tree_autoplay_switch_replay_and_end_frame(self):
        exe=ROOT/'build'/'rep_player.exe'
        self.assertTrue(exe.is_file(),'native window is not implemented')
        report=ROOT/'validation'/'ui_smoke.json'
        result=subprocess.run([str(exe),'--ui-test',str(report),'--client',r'D:\115us\client'],timeout=65,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr.decode(errors='replace'))
        out=json.loads(report.read_text(encoding='utf8'))
        expected=sum(1 for p in Path(r'D:\115us\client\Replay').rglob('*') if p.is_file() and p.suffix.lower()=='.rep')
        self.assertEqual(out['tree_replays'],expected)
        self.assertTrue(out['autoplay'] and out['switched'] and out['replayed'] and out['frozen'])
        self.assertLess(out['first_open_seconds'],2)
        self.assertLess(out['second_open_seconds'],2)
        self.assertLess(out['hot_replay_seconds'],2)
    def test_worker_recovers_from_error_in_a_later_scene(self):
        path=ROOT/'validation'/'ui_bad_second_scene.rep'
        # First scene renders successfully; second scene has an unbalanced push.
        path.write_bytes(pack_replay(1.7,{0:struct.pack('<I',17),1:struct.pack('<3I',0,1,0)},[(30,(0,),b''),(60,(1,),b'')]))
        report=ROOT/'validation'/'ui_recovery.json'
        result=subprocess.run([str(ROOT/'build'/'rep_player.exe'),'--ui-test',str(report),str(path),'--client',r'D:\115us\client'],timeout=65,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr.decode(errors='replace'))
        out=json.loads(report.read_text(encoding='utf8'))
        self.assertTrue(out['recovered_after_scene_error'] and out['autoplay'] and out['frozen'])
if __name__=='__main__':unittest.main()
