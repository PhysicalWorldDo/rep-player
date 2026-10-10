"""Negative IMG display settings exercised in isolated, owned native windows."""
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import shutil
import struct
import time
import unittest
import uuid

import test_canvas_ui as canvas_ui
import test_audio_ui as audio_ui
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


class NativeNegativeImgUiTests(canvas_ui.NativeCanvasUiTests):
    test_three_modes_drafts_apply_cancel_persistence_and_export_entry = None
    test_numeric_edit_space_does_not_toggle_paused_playback = None
    capture_control = audio_ui.NativeAudioUiTests.capture_control

    def setUp(self):
        self.folder = ROOT / 'validation' / 'negative_img_offsets_20261010' / ('ui_' + uuid.uuid4().hex[:10])
        self.folder.mkdir(parents=True)
        self.exe = self.folder / 'rep_player.exe'
        shutil.copy2(ROOT / 'build' / 'rep_player.exe', self.exe)
        resources = self.folder / 'resources'
        resources.mkdir()
        shutil.copy2(ROOT / 'build' / 'ffmpeg.exe', resources / 'ffmpeg.exe')
        self.client = self.folder / '中文客户端'
        packs = self.client / 'ImagePacks2'
        packs.mkdir(parents=True)
        replay_folder = self.client / 'Replay'
        replay_folder.mkdir()
        red, green = bytes((0, 0, 255, 255)) * 16, bytes((0, 255, 0, 255)) * 16
        image = canvas_ui.img_header(2, 72, 2)
        image += canvas_ui.rec(16, 5, 4, 4, 64, x=-1, y=0, full_w=4, full_h=4)
        image += canvas_ui.rec(16, 5, 4, 4, 64, x=0, y=0, full_w=4, full_h=4)
        image += red + green
        (packs / 'sprite_test.NPK').write_bytes(canvas_ui.npk(image, 'sprite/test/frame.img'))
        self.replay = replay_folder / 'negative.rep'
        self.other_replay = replay_folder / 'other.rep'
        self.write_replay()
        self.other_replay.write_bytes(self.replay.read_bytes())
        self.user = C.WinDLL('user32', use_last_error=True)
        self.callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        self.user.EnumWindows.argtypes = [self.callback, W.LPARAM]
        self.user.EnumChildWindows.argtypes = [W.HWND, self.callback, W.LPARAM]
        self.user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
        self.user.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
        self.user.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
        self.user.GetDlgItem.argtypes = [W.HWND, C.c_int]
        self.user.GetDlgItem.restype = W.HWND
        self.user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
        self.user.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
        self.user.SendMessageW.restype = W.LPARAM
        self.user.IsWindowVisible.argtypes = [W.HWND]
        self.user.IsWindowEnabled.argtypes = [W.HWND]
        self.user.EnableWindow.argtypes = [W.HWND, W.BOOL]
        self.user.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
        self.user.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
        self.process = None
        self.main = None
        self.snapshots = []

    def write_replay(self, duration=100):
        commands = {}
        for frame in (0, 1):
            params = bytearray(canvas_ui._default_draw_params(b''))
            struct.pack_into('<h', params, 4, frame)
            struct.pack_into('<2f', params, 28, 0, 0)
            commands[frame] = struct.pack('<II', 3, 64) + params
        auxiliary = struct.pack('<4h', 2, 3, 10, 3)
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        self.replay.write_bytes(canvas_ui.pack_replay(1.7, commands,
            [(0, (0, 1), auxiliary), (duration, (0, 1), auxiliary)],
            ['sprite/test/frame.img'], header=header))

    def start_player(self, ended=True):
        super().start_player(ended)
        # Only our PID's window is disabled; test WM_COMMAND still follows the
        # actual app path while unrelated desktop input cannot steer the test.
        self.user.EnableWindow(self.main, False)

    def require_button(self):
        button = self.control(self.main, 26)
        self.assertTrue(button, 'native UI is missing the authorized negative IMG offsets button')
        return button

    def frame_text(self):
        return next((self.text(w) for w, kind in self.windows(self.main)
                     if kind == 'Static' and self.text(w).startswith(('帧 ', 'Frame '))), '')

    def toggle(self, enabled):
        self.command(self.main, 26)
        suffix = ('On', '开') if enabled else ('Off', '关')
        self.wait(lambda: self.text(self.require_button()).endswith(suffix), 'negative IMG toggle state')
        settings = self.folder / 'runtime' / 'negative_img_offsets.txt'
        self.wait(lambda: settings.is_file() and settings.read_text().strip() == str(int(enabled)), 'saved negative IMG display choice')

    def export_png(self, name, toggle_during_export=None):
        self.wait(lambda: self.user.IsWindowEnabled(self.control(self.main, 11)), 'export available')
        self.command(self.main, 11)
        dialog = self.wait(lambda: next((w for w, kind in self.windows() if kind == 'RepExportSettings'), None), 'export dialog')
        selector = self.control(dialog, 101)
        self.user.SendMessageW(selector, 0x014E, 2, 0)  # PNG
        self.user.SendMessageW(dialog, 0x0111, 101 | (1 << 16), selector)
        self.edit(dialog, 106, name)
        self.command(dialog, 1)
        self.wait(lambda: not self.user.IsWindowVisible(dialog), 'export started')
        self.user.EnableWindow(self.main, False)
        if toggle_during_export is not None:
            self.toggle(toggle_during_export)
        target = self.folder / 'exports' / name
        self.wait(lambda: target.is_dir() and self.user.IsWindowEnabled(self.control(self.main, 11)), 'export completion', timeout=20)
        self.assertFalse(any(target.glob('*.partial')))
        frames = sorted(target.glob('frame_*.png'))
        self.assertTrue(frames)
        pixels = []
        for path in frames:
            with Image.open(path) as image:
                self.assertEqual(image.size, (16, 16), 'negative IMG offsets must not expand the output canvas')
                rgba = image.convert('RGBA')
                pixels.append(rgba.getpixel((2, 4)))
                self.assertEqual(rgba.getpixel((11, 4)), (0, 255, 0, 255), 'normal frame from the same IMG must remain visible')
        self.snapshots.append({'export': name, 'frames': len(frames), 'negative_pixels': pixels})
        return pixels

    def test_default_toggle_ended_step_persistence_and_png_snapshot(self):
        self.start_player()
        self.assertEqual(self.text(self.require_button()), '负坐标生效：关')
        self.assertIn('2 / 2', self.frame_text())
        self.assertTrue(all(p == (0, 0, 0, 0) for p in self.export_png('default_off')))
        self.toggle(True)
        self.assertIn('2 / 2', self.frame_text())
        self.assertEqual(self.text(self.control(self.main, 7)), '▶')
        self.assertTrue(all(p == (255, 0, 0, 255) for p in self.export_png('enabled_on')))
        self.command(self.main, 16)
        self.wait(lambda: '1 / 2' in self.frame_text(), 'previous frame applied')
        self.toggle(False)
        self.assertIn('1 / 2', self.frame_text())
        self.assertEqual(self.text(self.control(self.main, 7)), '▶')
        self.toggle(True)
        self.command(self.main, 14)  # Show all does not alter negative display.
        self.assertTrue(self.text(self.require_button()).endswith('开'))
        self.close_player()
        self.start_player()
        self.assertEqual(self.text(self.require_button()), '负坐标生效：开')
        # A running export must keep its start-time choice if the preview is
        # toggled immediately after the export dialog is accepted.
        self.assertTrue(all(p == (255, 0, 0, 255) for p in self.export_png('snapshot_on', toggle_during_export=False)))
        self.assertTrue(all(p == (0, 0, 0, 0) for p in self.export_png('next_export_off')))

    def test_bilingual_minimum_layout_refresh_and_replay_switch_keep_choice(self):
        self.start_player()
        self.require_button()
        (self.folder / 'runtime' / 'layout.txt').write_text('300 0.9', encoding='utf8')
        self.close_player()
        self.start_player()
        self.user.SetWindowPos(self.main, None, 0, 0, 1040, 720, 0x0002 | 0x0004)
        self.toggle(True)
        for locale, expected in ((9, '负坐标生效：开'), (10, 'Negative offsets: On')):
            self.command(self.main, locale)
            self.wait(lambda: self.text(self.require_button()) == expected, 'translated negative IMG toggle')
            time.sleep(.1)
            button = self.rect(self.require_button())
            bulk = self.rect(self.control(self.main, 18))
            image_host = self.rect(self.control(self.main, 1011))
            self.assertGreaterEqual(button.top, bulk.bottom + 6)
            self.assertLessEqual(button.bottom + 6, image_host.top)
            self.assertGreaterEqual(image_host.bottom - image_host.top, 42)
            self.assertLessEqual(button.right, self.rect(self.main).right)
            self.capture_control(self.main, 'minimum_' + ('zh' if locale == 9 else 'en'))
            self.snapshots.append({'locale': locale, 'button': expected, 'rect': [button.left, button.top, button.right, button.bottom], 'img_height': image_host.bottom - image_host.top})
        self.command(self.main, 25)
        self.wait(lambda: self.user.IsWindowEnabled(self.control(self.main, 25)), 'refresh finished')
        self.assertEqual(self.text(self.require_button()), 'Negative offsets: On')
        # Search and normal TreeView selection switch the actual REP, then the
        # toolbar must continue using the remembered global display choice.
        buffer = C.create_unicode_buffer('other.rep')
        self.user.SendMessageW(self.control(self.main, 8), 0x000C, 0, C.cast(buffer, C.c_void_p).value)
        tree = self.user.GetDlgItem(self.control(self.main, 1010), 4)
        leaf = self.user.SendMessageW(tree, 0x110A, 0, 0)  # TVGN_ROOT
        self.assertTrue(leaf)
        self.user.SendMessageW(tree, 0x110B, 9, leaf)  # TVM_SELECTITEM / TVGN_CARET
        # Unmapped English labels use the filename stem (Chinese keeps .rep).
        self.wait(lambda: self.text(self.main) == 'REP Player · other', 'other REP selected')
        self.assertEqual(self.text(self.require_button()), 'Negative offsets: On')
        self.close_player()
        self.start_player()
        self.assertEqual(self.text(self.require_button()), 'Negative offsets: On')

    def test_invalid_saved_choice_defaults_off(self):
        runtime = self.folder / 'runtime'
        runtime.mkdir()
        for malformed in ('2', '1 extra', 'invalid'):
            (runtime / 'negative_img_offsets.txt').write_text(malformed, encoding='utf8')
            self.start_player()
            self.assertEqual(self.text(self.require_button()), '负坐标生效：关')
            self.close_player()

    def test_paused_and_stopped_state_survives_toggle(self):
        self.write_replay(duration=60000)
        self.start_player(ended=False)
        self.require_button()
        self.command(self.main, 7)
        self.wait(lambda: '已暂停' in self.text(self.control(self.main, 6)), 'paused before changing IMG display')
        before = self.frame_text()
        self.toggle(True)
        self.assertIn('已暂停', self.text(self.control(self.main, 6)))
        self.assertEqual(self.frame_text(), before)
        self.command(self.main, 3)
        self.wait(lambda: '已停止' in self.text(self.control(self.main, 6)), 'stopped playback')
        self.toggle(False)
        self.assertIn('已停止', self.text(self.control(self.main, 6)))


if __name__ == '__main__':
    unittest.main()
