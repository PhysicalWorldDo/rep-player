"""Canvas controls exercised through a disposable copy of the actual native UI."""
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import unittest
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, r'D:\DNF115us\skill_player')
from test_rep_protocol_versions import pack_replay
from test_npk_reader import npk, img_header, rec
from rep_protocol import _default_draw_params

ROOT = Path(__file__).resolve().parents[1]


class NativeCanvasUiTests(unittest.TestCase):
    def setUp(self):
        self.folder = ROOT / 'validation' / 'canvas_ui' / uuid.uuid4().hex[:10]
        self.folder.mkdir(parents=True)
        self.exe = self.folder / 'rep_player.exe'
        shutil.copy2(ROOT / 'build' / 'rep_player.exe', self.exe)
        self.client = self.folder / 'client'
        assets = self.client / 'ImagePacks2'
        assets.mkdir(parents=True)
        replay_folder = self.client / 'Replay'
        replay_folder.mkdir()
        raw = bytes((0, 0, 255, 255)) * 16
        image = img_header(2, 36, 1) + rec(16, 5, 4, 4, len(raw), x=0, y=0, full_w=4, full_h=4) + raw
        (assets / 'sprite_test.NPK').write_bytes(npk(image, 'sprite/test/frame.img'))
        params = bytearray(_default_draw_params(b''))
        struct.pack_into('<2f', params, 28, 0, 0)
        draw = struct.pack('<II', 3, 64) + params
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        self.replay = replay_folder / 'canvas.rep'
        self.replay.write_bytes(pack_replay(1.7, {0: draw},
            [(0, (0,), struct.pack('<2h', 2, 3)), (100, (0,), struct.pack('<2h', 4, 3))],
            ['sprite/test/frame.img'], header=header))
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
        self.user.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
        self.user.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
        self.process = None
        self.main = None
        self.snapshots = []

    def tearDown(self):
        self.close_player()
        (self.folder / 'ui_evidence.json').write_text(json.dumps(self.snapshots, ensure_ascii=False, indent=2), encoding='utf8')

    def windows(self, parent=None):
        result = []
        @self.callback
        def visit(handle, unused):
            pid = W.DWORD()
            self.user.GetWindowThreadProcessId(handle, C.byref(pid))
            if pid.value == self.process.pid:
                name = C.create_unicode_buffer(256)
                self.user.GetClassNameW(handle, name, len(name))
                result.append((handle, name.value))
            return True
        if parent:
            self.user.EnumChildWindows(parent, visit, 0)
        else:
            self.user.EnumWindows(visit, 0)
        return result

    def wait(self, predicate, message, timeout=12):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            value = predicate()
            if value:
                return value
            if self.process.poll() is not None:
                self.fail('owned player exited while waiting for ' + message)
            time.sleep(.04)
        self.fail('timed out waiting for ' + message)

    def text(self, handle):
        if not handle:
            return ''
        buffer = C.create_unicode_buffer(4096)
        self.user.SendMessageW(handle, 0x000D, len(buffer), C.cast(buffer, C.c_void_p).value)
        return buffer.value

    def control(self, window, identifier):
        return self.user.GetDlgItem(window, identifier)

    def command(self, window, identifier):
        self.user.PostMessageW(window, 0x0111, identifier, 0)

    def edit(self, window, identifier, value):
        buffer = C.create_unicode_buffer(str(value))
        self.user.SendMessageW(self.control(window, identifier), 0x000C, 0, C.cast(buffer, C.c_void_p).value)

    def rect(self, handle):
        value = W.RECT()
        self.assertTrue(self.user.GetWindowRect(handle, C.byref(value)))
        return value

    def start_player(self, ended=True):
        self.process = subprocess.Popen([str(self.exe), '--client', str(self.client), '--open', str(self.replay)], cwd=self.folder)
        self.main = self.wait(lambda: next((w for w, kind in self.windows() if kind == 'NativeRepPlayer'), None), 'main window')
        if ended:
            self.wait(lambda: ('Ended' in self.text(self.control(self.main, 6)) or '播放结束' in self.text(self.control(self.main, 6))), 'fixture last frame')
        else:
            self.wait(lambda: ('Playing' in self.text(self.control(self.main, 6)) or '正在播放' in self.text(self.control(self.main, 6))), 'fixture playback')

    def close_player(self):
        if self.process and self.process.poll() is None:
            for handle, kind in self.windows():
                if kind in ('RepCanvasSettings', 'RepExportSettings'):
                    self.command(handle, 2)
            if self.main:
                self.user.PostMessageW(self.main, 0x10, 0, 0)
            try:
                self.process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                self.process.wait(timeout=5)
        self.process = None
        self.main = None

    def open_canvas(self, owner=None):
        self.command(owner or self.main, 107 if owner else 22)
        return self.wait(lambda: next((w for w, kind in self.windows() if kind == 'RepCanvasSettings'), None), 'canvas settings dialog')

    def result(self, dialog, width, height, left=None, top=None):
        self.wait(lambda: f'{width} × {height}' in self.text(self.control(dialog, 330)), 'resolved canvas size')
        if left is not None:
            origin = self.text(self.control(dialog, 331))
            self.assertIn(str(left), origin)
            self.assertIn(str(top), origin)
        self.snapshots.append({'size': self.text(self.control(dialog, 330)), 'origin': self.text(self.control(dialog, 331))})

    def test_three_modes_drafts_apply_cancel_persistence_and_export_entry(self):
        self.start_player()
        button = self.control(self.main, 22)
        self.assertTrue(button, 'native UI is missing the authorized Canvas settings toolbar button')
        self.user.SetWindowPos(self.main, None, 0, 0, 1040, 720, 0x0002 | 0x0004)
        time.sleep(.1)
        canvas_button, open_button = self.rect(button), self.rect(self.control(self.main, 21))
        self.assertLessEqual(canvas_button.right, open_button.left)
        self.assertEqual(canvas_button.top, open_button.top)
        # Footer controls retain their existing row, with no Canvas button inserted.
        previous, next_frame, export = (self.rect(self.control(self.main, i)) for i in (16, 17, 11))
        self.assertLess(previous.right, next_frame.left)
        self.assertLess(next_frame.right, export.left)
        dialog = self.open_canvas()
        self.result(dialog, 16, 16, 0, 0)
        self.command(dialog, 312)  # 2x
        self.result(dialog, 32, 32, 8, 8)
        self.command(dialog, 334)  # Apply remains open and saves.
        settings = self.folder / 'runtime' / 'canvas.txt'
        self.wait(lambda: settings.is_file() and float(settings.read_text().split()[1]) == 2, 'applied saved multiplier')
        self.assertTrue(self.user.IsWindowVisible(dialog))
        self.command(dialog, 302)
        self.edit(dialog, 320, 48)
        self.edit(dialog, 321, 40)
        self.result(dialog, 48, 40, 16, 12)
        self.edit(dialog, 320, 0)
        self.wait(lambda: not self.user.IsWindowEnabled(self.control(dialog, 334)), 'zero fixed width rejected')
        self.edit(dialog, 320, 8)
        self.edit(dialog, 321, 8)
        self.result(dialog, 16, 16, 0, 0)
        self.assertEqual(self.text(self.control(dialog, 320)), '8', 'larger REP must not overwrite the saved target width')
        self.edit(dialog, 320, 48)
        self.edit(dialog, 321, 40)
        self.command(dialog, 303)
        for identifier, value in ((322, 3), (323, 4), (324, 5), (325, 6)):
            self.edit(dialog, identifier, value)
        self.result(dialog, 24, 26, 3, 4)
        self.command(dialog, 302)
        self.result(dialog, 48, 40, 16, 12)
        self.command(dialog, 303)
        self.result(dialog, 24, 26, 3, 4)
        self.command(dialog, 334)
        self.wait(lambda: settings.read_text().split()[0] == '2', 'saved padding mode')
        self.command(dialog, 335)  # Restore changes only the unapplied draft.
        self.result(dialog, 16, 16, 0, 0)
        self.assertEqual(settings.read_text().split()[0], '2')
        self.command(dialog, 2)
        self.wait(lambda: not self.user.IsWindowVisible(dialog), 'cancelled canvas dialog')
        dialog = self.open_canvas()
        self.result(dialog, 24, 26, 3, 4)
        self.command(dialog, 302)
        self.result(dialog, 48, 40, 16, 12)
        self.command(dialog, 301)
        self.result(dialog, 32, 32, 8, 8)
        self.command(dialog, 303)
        self.command(dialog, 1)  # OK applies and closes.
        self.wait(lambda: not self.user.IsWindowVisible(dialog), 'accepted canvas dialog')
        self.close_player()
        self.start_player()
        dialog = self.open_canvas()
        self.result(dialog, 24, 26, 3, 4)
        self.command(dialog, 2)
        self.wait(lambda: not self.user.IsWindowVisible(dialog), 'canvas dialog closed before export')
        self.command(self.main, 11)
        exported = self.wait(lambda: next((w for w, kind in self.windows() if kind == 'RepExportSettings'), None), 'export settings dialog')
        size = self.text(self.control(exported, 108))
        self.assertIn('16 × 16', size)
        self.assertIn('24 × 26', size)
        dialog = self.open_canvas(exported)
        self.command(dialog, 335)
        self.command(dialog, 1)
        self.wait(lambda: '16 × 16' in self.text(self.control(exported, 108)) and '24 × 26' not in self.text(self.control(exported, 108)), 'export dimensions updated after Canvas OK')
        self.command(exported, 2)

    def test_numeric_edit_space_does_not_toggle_paused_playback(self):
        params = bytearray(_default_draw_params(b''))
        struct.pack_into('<2f', params, 28, 0, 0)
        draw = struct.pack('<II', 3, 64) + params
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        self.replay.write_bytes(pack_replay(1.7, {0: draw},
            [(0, (0,), struct.pack('<2h', 2, 3)), (60000, (0,), struct.pack('<2h', 4, 3))],
            ['sprite/test/frame.img'], header=header))
        self.start_player(ended=False)
        self.assertTrue(self.control(self.main, 22), 'Canvas settings toolbar button is missing')
        self.command(self.main, 7)
        paused = lambda: 'Paused' in self.text(self.control(self.main, 6)) or '已暂停' in self.text(self.control(self.main, 6))
        self.wait(paused, 'paused replay before numeric keyboard input')
        dialog = self.open_canvas()
        self.command(dialog, 302)
        self.wait(lambda: self.user.IsWindowVisible(self.control(dialog, 320)), 'numeric width editor')
        edit = self.control(dialog, 320)
        before = self.text(edit)
        kernel = C.WinDLL('kernel32')
        kernel.GetCurrentThreadId.restype = W.DWORD
        self.user.AttachThreadInput.argtypes = [W.DWORD, W.DWORD, W.BOOL]
        self.user.SetFocus.argtypes = [W.HWND]
        self.user.SetFocus.restype = W.HWND
        self.user.GetFocus.restype = W.HWND
        current_thread = kernel.GetCurrentThreadId()
        ui_thread = self.user.GetWindowThreadProcessId(dialog, None)
        self.assertTrue(self.user.AttachThreadInput(current_thread, ui_thread, True))
        try:
            self.user.SetFocus(edit)
            self.assertEqual(self.user.GetFocus(), edit)
        finally:
            self.user.AttachThreadInput(current_thread, ui_thread, False)
        self.user.PostMessageW(edit, 0x0100, 0x20, 1)
        self.user.PostMessageW(edit, 0x0101, 0x20, 1 | (1 << 30) | (1 << 31))
        time.sleep(.2)
        self.assertTrue(paused(), 'space in a Canvas numeric editor must not resume playback')
        self.assertEqual(self.text(edit), before, 'the numeric editor must ignore nonnumeric space input')
        self.command(dialog, 2)


if __name__ == '__main__':
    unittest.main()
