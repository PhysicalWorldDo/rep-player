"""Patch selection applies only through Refresh NPK in isolated real player windows."""
import ctypes as C
from ctypes import wintypes as W
import shutil
import struct
import time
import unittest

import test_canvas_ui as canvas_ui
import test_audio_ui as audio_ui
from PIL import Image

ROOT = canvas_ui.ROOT


class NativeNpkPatchesUiTests(canvas_ui.NativeCanvasUiTests):
    test_three_modes_drafts_apply_cancel_persistence_and_export_entry = None
    test_numeric_edit_space_does_not_toggle_paused_playback = None
    capture_control = audio_ui.NativeAudioUiTests.capture_control

    def setUp(self):
        super().setUp()
        resources = self.folder / 'resources'
        resources.mkdir()
        shutil.copy2(ROOT / 'build/ffmpeg.exe', resources / 'ffmpeg.exe')
        raw = bytes((0, 255, 0, 255)) * 16
        image = canvas_ui.img_header(2, 36, 1) + canvas_ui.rec(16, 5, 4, 4, 64, x=0, y=0, full_w=4, full_h=4) + raw
        self.patch = self.client / 'ImagePacks2/!patch.npk'
        self.patch.write_bytes(canvas_ui.npk(image, 'sprite/test/frame.img'))
        self.other = self.client / 'Replay/other.rep'
        self.other.write_bytes(self.replay.read_bytes())
        self.user.EnableWindow.argtypes = [W.HWND, W.BOOL]
        self.settings = self.folder / 'runtime/npk_patches.txt'
        print('NPK_PATCHES_UI_VALIDATION=' + str(self.folder), flush=True)

    def start_player(self, ended=True):
        super().start_player(ended)
        self.user.EnableWindow(self.main, False)

    def require_button(self):
        button = self.control(self.main, 27)
        self.assertTrue(button, 'native UI is missing the authorized Enable patches control')
        return button

    def selected(self):
        return self.user.SendMessageW(self.require_button(), 0x00F0, 0, 0) == 1  # BM_GETCHECK

    def toggle(self, enabled):
        self.command(self.main, 27)
        self.wait(lambda: self.selected() == enabled, 'pending patch selection')

    def refresh(self):
        self.command(self.main, 25)
        self.wait(lambda: self.settings.is_file() and self.settings.read_text().strip() == str(int(self.selected()))
                  and self.user.IsWindowEnabled(self.control(self.main, 25)), 'applied patch setting')
        self.wait(lambda: '播放结束' in self.text(self.control(self.main, 6)) or 'Ended' in self.text(self.control(self.main, 6)), 'refreshed last frame')

    def export_color(self, name, color):
        self.preview_color(name, color)
        self.wait(lambda: self.user.IsWindowEnabled(self.control(self.main, 11)), 'export enabled')
        self.command(self.main, 11)
        dialog = self.wait(lambda: next((w for w, kind in self.windows() if kind == 'RepExportSettings'), None), 'export dialog')
        selector = self.control(dialog, 101)
        self.user.SendMessageW(selector, 0x014E, 2, 0)
        self.user.SendMessageW(dialog, 0x0111, 101 | (1 << 16), selector)
        self.user.SendMessageW(self.control(dialog, 102), 0x014E, 1, 0)  # 30 FPS
        self.edit(dialog, 106, name)
        self.command(dialog, 1)
        self.wait(lambda: not self.user.IsWindowVisible(dialog), 'export accepted')
        self.user.EnableWindow(self.main, False)
        target = self.folder / 'exports' / name
        self.wait(lambda: target.is_dir() and self.user.IsWindowEnabled(self.control(self.main, 11)), 'export finished', timeout=20)
        frames = sorted(target.glob('frame_*.png'))
        self.assertEqual(len(frames), 4)
        for frame in frames:
            with Image.open(frame) as image:
                self.assertEqual(image.convert('RGBA').getpixel((5, 4)), color)
        self.snapshots.append({'export': name, 'color': color, 'frames': len(frames)})

    def preview_color(self, name, color):
        image = self.capture_control(self.control(self.main, 5), name + '_preview')
        colors = image.getcolors(image.width * image.height)
        self.assertTrue(any(count > 10 and rgb == color[:3] for count, rgb in colors), 'preview does not use the applied NPK color')
        opposite = (255, 0, 0) if color[0] == 0 else (0, 255, 0)
        self.assertFalse(any(count > 10 and rgb == opposite for count, rgb in colors), 'preview retains the old NPK color')

    def replay_end(self):
        self.command(self.main, 2)
        time.sleep(.2)
        self.wait(lambda: self.text(self.control(self.main, 7)) == '▶', 'replay ended')

    def test_default_pending_refresh_round_trip_and_shared_exports(self):
        self.start_player()
        self.assertTrue(self.selected())
        green, red = (0, 255, 0, 255), (255, 0, 0, 255)
        self.export_color('default_enabled', green)
        self.toggle(False)
        self.assertIn('待刷新', self.text(self.control(self.main, 6)))
        self.assertEqual(self.settings.read_text().strip(), '1')
        self.replay_end()
        self.export_color('pending_disable', green)
        self.refresh()
        self.assertNotIn('待刷新', self.text(self.control(self.main, 6)))
        self.command(self.main, 16)
        self.wait(lambda: any('1 / 2' in self.text(w) for w, kind in self.windows(self.main) if kind == 'Static'), 'previous frame after disabling patches')
        self.preview_color('previous_disabled', red)
        self.command(self.main, 17)
        self.wait(lambda: any('2 / 2' in self.text(w) for w, kind in self.windows(self.main) if kind == 'Static'), 'next frame after disabling patches')
        self.export_color('applied_disabled', red)
        self.toggle(True)
        self.export_color('pending_enable', red)
        self.refresh()
        self.export_color('applied_enabled', green)

    def test_restart_restores_applied_choice_and_discards_pending_choice(self):
        self.start_player()
        self.toggle(False)
        self.refresh()
        self.toggle(True)
        self.close_player()
        self.start_player()
        self.assertFalse(self.selected())
        self.export_color('restored_disabled', (255, 0, 0, 255))

    def test_pending_choice_survives_rep_switch_but_index_stays_applied(self):
        self.start_player()
        self.toggle(False)
        self.edit(self.main, 8, 'other.rep')
        tree = self.control(self.control(self.main, 1010), 4)
        leaf = self.user.SendMessageW(tree, 0x110A, 0, 0)
        self.assertTrue(leaf)
        self.user.SendMessageW(tree, 0x110B, 9, leaf)
        self.wait(lambda: 'other' in self.text(self.main), 'other REP selected')
        self.wait(lambda: '播放结束' in self.text(self.control(self.main, 6)), 'other REP ended')
        self.assertFalse(self.selected())
        self.export_color('switched_pending', (0, 255, 0, 255))
        self.refresh()
        self.export_color('switched_applied', (255, 0, 0, 255))

    def test_bilingual_minimum_toolbar_and_invalid_settings_default_enabled(self):
        runtime = self.folder / 'runtime'
        runtime.mkdir()
        for malformed in ('2', '0 extra', 'invalid'):
            self.settings.write_text(malformed, encoding='utf8')
            self.start_player()
            self.assertTrue(self.selected())
            self.close_player()
        self.start_player()
        self.user.SetWindowPos(self.main, None, 0, 0, 1040, 720, 0x0002 | 0x0004)
        for locale, expected in ((9, '启用补丁'), (10, 'Enable patches')):
            self.command(self.main, locale)
            self.wait(lambda: self.text(self.require_button()) == expected, 'patch control translation')
            controls = [self.require_button()] + [self.control(self.main, i) for i in (25, 22, 21, 1, 9, 10)]
            for left, right in zip(controls, controls[1:]):
                self.assertLessEqual(self.rect(left).right, self.rect(right).left)
            self.capture_control(self.main, 'patch_toolbar_' + str(locale))

    def test_failed_refresh_keeps_saved_choice_and_disabled_mode_recovers(self):
        self.start_player()
        self.toggle(False)
        self.refresh()
        (self.client / 'ImagePacks2/!!broken.npk').write_bytes(b'broken patch fixture')
        self.toggle(True)
        self.command(self.main, 25)
        self.wait(lambda: '!!broken.npk' in self.text(self.control(self.main, 6)), 'failed patch scan')
        self.assertEqual(self.settings.read_text().strip(), '0')
        self.assertTrue(self.selected())
        self.assertIn('待刷新', self.text(self.control(self.main, 6)))
        self.toggle(False)
        self.refresh()
        self.export_color('recovered_disabled', (255, 0, 0, 255))

    def test_loading_and_running_export_block_patch_changes(self):
        self.start_player()
        self.user.SendMessageW(self.main, 0x0111, 25, 0)
        self.assertFalse(self.user.IsWindowEnabled(self.require_button()))
        self.assertFalse(self.user.IsWindowEnabled(self.control(self.main, 25)))
        self.user.SendMessageW(self.main, 0x0111, 27, 0)
        self.assertTrue(self.selected())
        self.refresh()
        self.close_player()
        params = bytearray(canvas_ui._default_draw_params(b''))
        struct.pack_into('<2f', params, 28, 0, 0)
        draw = struct.pack('<II', 3, 64) + params
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        self.replay.write_bytes(canvas_ui.pack_replay(1.7, {0: draw},
            [(0, (0,), struct.pack('<2h', 2, 3)), (60000, (0,), struct.pack('<2h', 4, 3))],
            ['sprite/test/frame.img'], header=header))
        self.start_player(ended=False)
        self.command(self.main, 11)
        dialog = self.wait(lambda: next((w for w, kind in self.windows() if kind == 'RepExportSettings'), None), 'long export dialog')
        selector = self.control(dialog, 101)
        self.user.SendMessageW(selector, 0x014E, 2, 0)
        self.user.SendMessageW(dialog, 0x0111, 101 | (1 << 16), selector)
        self.edit(dialog, 106, 'owned_cancelled_export')
        self.command(dialog, 1)
        self.wait(lambda: not self.user.IsWindowVisible(dialog), 'long export started')
        self.wait(lambda: not self.user.IsWindowEnabled(self.require_button()), 'patch control disabled during export')
        self.assertFalse(self.user.IsWindowEnabled(self.control(self.main, 25)))
        self.user.SendMessageW(self.main, 0x0111, 27, 0)
        self.user.SendMessageW(self.main, 0x0111, 25, 0)
        self.assertTrue(self.selected())
        self.assertEqual(self.settings.read_text().strip(), '1')


if __name__ == '__main__':
    unittest.main()
