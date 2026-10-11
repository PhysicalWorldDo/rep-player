"""Navigation regressions through disposable copies of the native Win32 UI."""
import ctypes as C
from contextlib import contextmanager
from ctypes import wintypes as W
import os
from pathlib import Path
import re
import shutil
import struct
import time
import unittest
import uuid

import test_canvas_ui as canvas_ui
import test_audio_ui as audio_ui

ROOT = canvas_ui.ROOT


class OwnedPlayerUi(canvas_ui.NativeCanvasUiTests):
    """Shared owned-PID fixture; inherited Canvas test cases are excluded."""
    test_three_modes_drafts_apply_cancel_persistence_and_export_entry = None
    test_numeric_edit_space_does_not_toggle_paused_playback = None
    capture_control = audio_ui.NativeAudioUiTests.capture_control
    suite_name = 'navigation_ui_20261011'

    def setUp(self):
        self.folder = ROOT / 'validation' / self.suite_name / ('ui_' + uuid.uuid4().hex[:10])
        self.folder.mkdir(parents=True)
        self.exe = self.folder / 'rep_player.exe'
        source = Path(os.environ.get('REP_PLAYER_UI_EXE', ROOT / 'build' / 'rep_player.exe'))
        shutil.copy2(source, self.exe)
        resources = self.folder / 'resources'
        resources.mkdir()
        shutil.copy2(ROOT / 'build' / 'ffmpeg.exe', resources / 'ffmpeg.exe')
        self.client = self.folder / 'client'
        packs = self.client / 'ImagePacks2'
        packs.mkdir(parents=True)
        replay_folder = self.client / 'Replay'
        replay_folder.mkdir()
        raw = bytes((0, 0, 255, 255)) * 16
        image = canvas_ui.img_header(2, 36, 1) + canvas_ui.rec(16, 5, 4, 4, 64, x=0, y=0, full_w=4, full_h=4) + raw
        (packs / 'sprite_test.NPK').write_bytes(canvas_ui.npk(image, 'sprite/test/frame.img'))
        self.replay = replay_folder / 'navigation.rep'
        self.write_navigation_replay()
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
        self.user.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
        self.user.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
        self.process = None
        self.main = None
        self.snapshots = []
        print('OWNED_UI_VALIDATION=' + str(self.folder), flush=True)

    def write_navigation_replay(self, timestamps=(0, 20000, 30000, 60000, 60000)):
        self.record_count = len(timestamps)
        params = bytearray(canvas_ui._default_draw_params(b''))
        struct.pack_into('<2f', params, 28, 0, 0)
        draw = struct.pack('<II', 3, 64) + params
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        self.replay.write_bytes(canvas_ui.pack_replay(1.7, {0: draw},
            [(stamp, (0,), struct.pack('<2h', 2 + index, 3)) for index, stamp in enumerate(timestamps)],
            ['sprite/test/frame.img'], header=header))

    def start_player(self, ended=False):
        super().start_player(ended)
        self.user.EnableWindow(self.main, False)

    def frame_number(self):
        for handle, kind in self.windows(self.main):
            if kind == 'Static':
                match = re.match(r'(?:帧|Frame)\s+(\d+)\s*/\s*(\d+)', self.text(handle))
                if match:
                    return int(match[1]), int(match[2])
        return None

    def wait_frame(self, ordinal):
        self.wait(lambda: self.frame_number() == (ordinal, self.record_count), 'record ordinal ' + str(ordinal))

    def phase(self, name):
        labels = {'Playing': '正在播放', 'Paused': '已暂停', 'Stopped': '已停止', 'Ended': '播放结束'}
        status = self.text(self.control(self.main, 6))
        return name in status or labels[name] in status

    def wait_phase(self, name):
        self.wait(lambda: self.phase(name), name + ' playback phase')

    @contextmanager
    def focused(self, handle):
        # Attach only to the thread of the player PID owned by this fixture.
        pid = W.DWORD()
        thread = self.user.GetWindowThreadProcessId(handle, C.byref(pid))
        self.assertEqual(pid.value, self.process.pid)
        kernel = C.WinDLL('kernel32', use_last_error=True)
        kernel.GetCurrentThreadId.restype = W.DWORD
        self.user.AttachThreadInput.argtypes = [W.DWORD, W.DWORD, W.BOOL]
        self.user.SetFocus.argtypes = [W.HWND]
        self.user.SetFocus.restype = W.HWND
        self.user.GetFocus.restype = W.HWND
        current = kernel.GetCurrentThreadId()
        self.user.EnableWindow(self.main, True)
        self.assertTrue(self.user.AttachThreadInput(current, thread, True))
        try:
            self.user.SetFocus(handle)
            self.assertEqual(self.user.GetFocus(), handle)
            yield
        finally:
            self.user.AttachThreadInput(current, thread, False)
            self.user.EnableWindow(self.main, False)

    def key(self, handle, code, repeats=0):
        self.user.PostMessageW(handle, 0x0100, code, 1)
        for unused in range(repeats):
            self.user.PostMessageW(handle, 0x0100, code, 1 | (1 << 30))
        self.user.PostMessageW(handle, 0x0101, code, 1 | (1 << 30) | (1 << 31))

    def progress_point(self, fraction):
        progress = self.require_progress()
        rectangle = W.RECT()
        self.assertTrue(self.user.GetClientRect(progress, C.byref(rectangle)))
        # Midpoint is chosen on its left pixel to remain at or below 30 seconds.
        x = int((rectangle.right - 1) * fraction)
        if fraction == .5:
            x = max(0, x - 1)
        y = rectangle.bottom // 2
        return progress, x | (y << 16)

    def seek_click(self, fraction):
        progress, point = self.progress_point(fraction)
        self.user.SendMessageW(progress, 0x0201, 1, point)
        self.user.SendMessageW(progress, 0x0202, 0, point)

    def require_progress(self):
        progress = self.control(self.main, 28)
        self.assertTrue(progress, 'native UI is missing the authorized clickable progress control')
        return progress


class NativeNavigationUiTests(OwnedPlayerUi):
    def test_progress_is_a_mouse_interaction_control(self):
        self.start_player()
        self.require_progress()

    def test_click_zero_middle_and_last_duplicate_record_while_paused(self):
        self.start_player()
        self.command(self.main, 7)
        self.wait_phase('Paused')
        for fraction, ordinal in ((0, 1), (.5, 3), (1, 5), (0, 1)):
            self.seek_click(fraction)
            self.wait_frame(ordinal)
            self.assertTrue(self.phase('Paused'))
        self.snapshots.append({'clicks': [0, .5, 1, 0], 'last_duplicate_ordinal': 5})

    def test_drag_changes_target_without_rebuilding_until_release(self):
        self.start_player()
        self.command(self.main, 7)
        self.wait_phase('Paused')
        before = self.frame_number()
        progress, point = self.progress_point(.75)
        self.user.SendMessageW(progress, 0x0201, 1, point)
        self.wait(lambda: self.text(progress).startswith(('目标 ', 'Target ')), 'visible drag target')
        first_target = self.text(progress)
        progress, point = self.progress_point(.25)
        self.user.SendMessageW(progress, 0x0200, 1, point)
        self.wait(lambda: self.text(progress) != first_target, 'target follows continuous drag')
        time.sleep(.15)
        self.assertEqual(self.frame_number(), before, 'dragging must not rebuild every mouse movement')
        progress, point = self.progress_point(.5)
        self.user.SendMessageW(progress, 0x0200, 1, point)
        self.user.SendMessageW(progress, 0x0202, 0, point)
        self.wait_frame(3)
        self.assertTrue(self.phase('Paused'))
        self.snapshots.append({'drag_first_target': first_target, 'released_target': self.text(progress)})

    def test_seek_resumes_playing_and_turns_stopped_or_ended_into_paused(self):
        self.start_player()
        self.seek_click(.25)
        self.wait_phase('Playing')
        self.command(self.main, 3)
        self.wait_phase('Stopped')
        self.seek_click(.5)
        self.wait_frame(3)
        self.wait_phase('Paused')
        self.close_player()
        self.write_navigation_replay((0, 100, 100, 200, 200))
        self.start_player(ended=True)
        self.seek_click(0)
        self.wait_frame(1)
        self.wait_phase('Paused')

    def test_arrows_work_in_each_nontext_pane_and_keep_search_caret_native(self):
        self.write_navigation_replay((0, 100, 100, 200, 200))
        self.start_player(ended=True)
        controls = [self.control(self.main, 5), self.control(self.control(self.main, 1010), 4),
                    self.control(self.control(self.main, 1011), 15)]
        controls += [self.control(self.main, identifier) for identifier in (7, 18, 26, 27, 28)]
        for handle in controls:
            self.assertTrue(handle, 'all authorized nontext panes must exist')
            with self.focused(handle):
                self.key(handle, 0x25)
                self.wait_frame(4)
                self.key(handle, 0x27)
                self.wait_frame(5)
            self.assertTrue(self.phase('Paused'))
        search = self.control(self.main, 8)
        self.edit(self.main, 8, 'navigation')
        with self.focused(search):
            self.user.SendMessageW(search, 0x00B1, 3, 3)  # EM_SETSEL
            self.key(search, 0x25)
            self.wait(lambda: self.user.SendMessageW(search, 0x00B0, 0, 0) == (2 | (2 << 16)), 'native search caret left')
            self.key(search, 0x27)
            self.wait(lambda: self.user.SendMessageW(search, 0x00B0, 0, 0) == (3 | (3 << 16)), 'native search caret right')
        self.assertEqual(self.frame_number(), (5, 5), 'search arrows must not step playback')

    def test_repeat_burst_stops_on_keyup_without_a_delayed_step_backlog(self):
        self.write_navigation_replay(tuple(index * 5 for index in range(200)))
        self.start_player(ended=True)
        handle = self.control(self.main, 18)
        with self.focused(handle):
            self.key(handle, 0x25, repeats=100)
            self.wait(lambda: self.frame_number()[0] < 200, 'first held arrow step')
            time.sleep(.3)
            settled = self.frame_number()
            self.assertGreaterEqual(settled[0], 197, 'a busy repeat burst must not queue every OS key event')
            time.sleep(.5)
            self.assertEqual(self.frame_number(), settled, 'keyup must leave no deferred repeat backlog')
        self.snapshots.append({'after_repeat_keyup': settled})


if __name__ == '__main__':
    unittest.main()
