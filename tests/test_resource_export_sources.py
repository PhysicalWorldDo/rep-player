"""Export source selection follows the applied playback indexes."""
from pathlib import Path
import shutil
import subprocess
import unittest
import uuid

from ogg_fixtures import make_fixture
from test_npk_priority import GREEN, RED, image, write_native_index, write_package


ROOT = Path(__file__).resolve().parents[1]
IMG = 'sprite/interface/test/frame.img'
HD = 'sprite/interface/test/(2x)frame.img'
PALETTE = 'sprite/interface/test/frame0100.img'
SOUND = 'sounds/effect/fixture/tone_a.ogg'


class ResourceExportSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'resource_export_sources' / uuid.uuid4().hex[:10]
        cls.folder.mkdir(parents=True)
        cls.exe = cls.folder / 'probe.exe'
        cls.ffmpeg = cls.folder / 'ffmpeg.exe'
        shutil.copy2(ROOT / 'build/ffmpeg.exe', cls.ffmpeg)
        cls.audio_fixture = make_fixture(cls.folder / 'audio_fixture', cls.ffmpeg)
        compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        subprocess.run(list(map(str, [compiler, '-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE',
            '-municode', '-static', '-I', ROOT / 'src', '-I', ROOT / 'vendor/zlib',
            ROOT / 'src/protocol.cpp', ROOT / 'src/resources.cpp', ROOT / 'src/audio_resources.cpp',
            ROOT / 'tests/resource_export_sources_native.cpp', ROOT / 'vendor/zlib/libz.a', '-o', cls.exe])),
            check=True, timeout=120)

    def setUp(self):
        self.client = self.folder / self._testMethodName
        self.images = self.client / 'ImagePacks2'
        self.sounds = self.client / 'SoundPacks'
        self.images.mkdir(parents=True)
        self.sounds.mkdir()

    def read(self, kind, path, patches=True, frames=(), expected_status=0):
        output = self.client / 'output.raw'
        completed = subprocess.run(list(map(str, [self.exe, kind, self.client, path, int(patches), output, *frames])),
                                   capture_output=True, text=True, timeout=30)
        self.assertEqual(completed.returncode, expected_status, completed.stderr)
        if not expected_status:
            return completed.stdout.strip(), output.read_bytes()
        self.assertFalse(output.exists())

    def image_sources(self, official, patch):
        name = 'sprite_interface_test.NPK'
        locations = write_package(self.images / name, official)
        write_native_index(self.images / 'NpkIndex.etc', name, locations)
        write_package(self.images / '!patch.npk', patch)

    def audio_sources(self, patch):
        original = self.audio_fixture['oggA'].read_bytes()
        (self.client / 'audio.xml').write_text(f'<AUDIO><EFFECT ID="TONE" FILE="{SOUND}"/></AUDIO>', encoding='utf8')
        write_package(self.sounds / 'sounds_effect_fixture.npk', {SOUND: original})
        write_package(self.sounds / '!patch.npk', {SOUND: patch})
        return original

    def test_img_preserves_applied_patch_bytes(self):
        original, patch = image(RED), image(GREEN)
        self.image_sources({IMG: original}, {IMG: patch})
        self.assertEqual(self.read('img', IMG), (IMG, patch))
        self.assertEqual(self.read('img', IMG, False), (IMG, original))

    def test_img_falls_back_when_patch_is_too_short_for_current_frame(self):
        original, patch = image(RED, RED), image(GREEN)
        self.image_sources({IMG: original}, {IMG: patch})
        self.assertEqual(self.read('img', IMG, frames=(0, 1)), (IMG, original))

    def test_img_falls_back_when_patch_is_unreadable(self):
        original = image(RED)
        self.image_sources({IMG: original}, {IMG: b'broken IMG'})
        self.assertEqual(self.read('img', IMG), (IMG, original))

    def test_img_preserves_the_effective_2x_and_palette_entry_names(self):
        patch = image(GREEN)
        self.image_sources({HD: image(RED), PALETTE: image(RED)}, {HD: patch, PALETTE: patch})
        self.assertEqual(self.read('img', IMG, frames=(0,)), (HD, patch))
        self.assertEqual(self.read('img', PALETTE.replace('0100', '0101')), (PALETTE, patch))

    def test_missing_img_is_unavailable(self):
        self.read('img', IMG, expected_status=2)

    def test_ogg_preserves_applied_patch_bytes(self):
        patch = self.audio_fixture['oggB'].read_bytes()
        original = self.audio_sources(patch)
        self.assertEqual(self.read('ogg', SOUND), (SOUND, patch))
        self.assertEqual(self.read('ogg', SOUND, False), (SOUND, original))

    def test_ogg_falls_back_when_patch_is_unplayable(self):
        original = self.audio_sources(b'broken ogg')
        self.assertEqual(self.read('ogg', SOUND), (SOUND, original))

    def test_ogg_keeps_playback_rejection_until_refresh(self):
        valid = self.audio_fixture['oggB'].read_bytes()
        original = self.audio_sources(b'\0' * len(valid))
        write_package(self.client / 'valid.npk', {SOUND: valid})
        self.assertEqual(self.read('rejected-ogg', SOUND), (SOUND, original))
        self.assertEqual(self.read('ogg', SOUND), (SOUND, valid))

    def test_ogg_keeps_loose_music_bytes(self):
        path = self.client / 'Music' / 'theme.ogg'
        path.parent.mkdir()
        wrapped = b'Neople Music File\0\0\0\1\0\0\0' + bytes(range(16))
        path.write_bytes(wrapped)
        self.assertEqual(self.read('ogg', 'music/theme.ogg'), ('music/theme.ogg', wrapped))


if __name__ == '__main__':
    unittest.main()
