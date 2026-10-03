import json
from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
class NativeMovieTests(unittest.TestCase):
    def test_real_avi_and_bink_decode_timestamp_without_python_runtime(self):
        for name,width,height,count in [('Video/Adventurer/fighter_m.avi',800,450,210),('Video/CharacterCutScene/03mghost_buf_bsk.bk2',970,668,61)]:
            with self.subTest(name=name):
                result=subprocess.run([str(ROOT/'build'/'rep_gpu.exe'),'--movie',name],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertTrue(result.stdout.strip(),'native movie path is not implemented')
                got=json.loads(result.stdout)
                self.assertEqual((got['width'],got['height'],got['frames']),(width,height,count))
                self.assertNotEqual(got['first_crc32'],got['later_crc32'])
                self.assertEqual(got['first_crc32'],got['rewind_crc32'])
if __name__=='__main__':unittest.main()
