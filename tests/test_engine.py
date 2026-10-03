import json
from pathlib import Path
import subprocess
import unittest
import struct
import sys
sys.dont_write_bytecode=True
sys.path.insert(0,r'D:\DNF115us\skill_player')
from test_rep_protocol_versions import pack_replay

ROOT = Path(__file__).resolve().parents[1]
CLIENT = Path(r'D:\115us\client\Replay\SkillReplay')

class NativeEngineTests(unittest.TestCase):
    def test_bloodyrave_suction_is_in_front_of_hand_and_target(self):
        # Original scene 65 (1116 ms): cunsume/cunsume2 have explicit pivots,
        # negative scales and recorded +90-degree rotations. The client
        # negates the angle; the vortex belongs around the target, not the NPC.
        folder=ROOT/'validation'/'bloodyrave_fix'
        folder.mkdir(exist_ok=True)
        report=folder/'real_scene65_regression.json'
        rgba=folder/'real_scene65_regression.rgba'
        result=subprocess.run([str(ROOT/'build'/'rep_gpu.exe'),'--consume',
                               str(CLIENT/'Swordman'/'BloodyRave.rep'),str(report),
                               str(rgba),r'D:\115us\client\ImagePacks2','65'],
                              capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        data=json.loads(report.read_text())
        self.assertEqual((data['width'],data['height']),(800,600))
        pixels=rgba.read_bytes()
        def red_pixels(rect):
            left,top,right,bottom=rect
            return sum(r>100 and r>2.5*g and r>2.5*b
                       for y in range(top,bottom) for x in range(left,right)
                       for r,g,b in [pixels[(y*800+x)*4:(y*800+x)*4+3]])
        self.assertGreater(red_pixels((250,300,510,430)),3500,
                           'red suction is missing from the hand/target region')
        self.assertLess(red_pixels((0,490,210,600)),100,
                        'red suction is incorrectly drawn over the lower-left NPC')

    def test_realtime_selection_skips_intermediate_scenes_and_rewinds(self):
        path=ROOT/'validation'/'selection.rep'
        path.write_bytes(pack_replay(1.7,{0:struct.pack('<I',17)},[(t,(0,),b'') for t in (10,10,30,40)]))
        result=subprocess.run([str(ROOT/'build'/'rep_gpu.exe'),'--select',str(path),'0,5,10,11,30,31,50'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        data=json.loads(result.stdout)
        self.assertEqual(data['timestamps'],[10,10,10,30,30,40,40])
        self.assertEqual(data['changed'],[True,False,False,True,False,True,False])
        self.assertEqual(data['selected'],3);self.assertEqual(data['skipped'],1)
        self.assertTrue(data['ended']);self.assertEqual(data['rewind_timestamp'],10)
    def test_three_priority_replays_execute_all_scenes_on_gpu(self):
        exe = ROOT / 'build' / 'rep_gpu.exe'
        for relative, scenes in [('Swordman/BloodSword.rep',188),('Priest/DivinePunishment.rep',557),('Gunner/ReturnedSniper.rep',211)]:
            with self.subTest(replay=relative):
                out = ROOT / 'validation' / (Path(relative).stem + '.json')
                result = subprocess.run([str(exe), '--consume', str(CLIENT/relative), str(out)], cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(out.is_file(), 'native scene execution is not implemented')
                report = json.loads(out.read_text(encoding='utf8'))
                self.assertEqual(report['scenes'],scenes)
                self.assertGreater(report['gpu_draws'],scenes)
                self.assertTrue(report['exact_eof'])

if __name__=='__main__':
    unittest.main()
