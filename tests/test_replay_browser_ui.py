"""Exercise the real Win32 tree and REP file dialog in owned test windows."""
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import struct
import subprocess
import sys
import time
import unittest
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, r'D:\DNF115us\skill_player')
from test_rep_protocol_versions import pack_replay

ROOT = Path(__file__).resolve().parents[1]
U, K = C.WinDLL('user32', use_last_error=True), C.WinDLL('kernel32', use_last_error=True)
CALLBACK = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
U.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
U.SendMessageW.restype = C.c_ssize_t
U.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
U.GetDlgItem.argtypes = [W.HWND, C.c_int]
U.GetDlgItem.restype = W.HWND
K.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
K.OpenProcess.restype = W.HANDLE
K.VirtualAllocEx.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD, W.DWORD]
K.VirtualAllocEx.restype = C.c_void_p
K.VirtualFreeEx.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD]
K.WriteProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p]
K.ReadProcessMemory.argtypes = K.WriteProcessMemory.argtypes
K.CloseHandle.argtypes = [W.HANDLE]


def label(hwnd):
    text = C.create_unicode_buffer(2048)
    U.GetWindowTextW(hwnd, text, len(text))
    return text.value


def class_name(hwnd):
    text = C.create_unicode_buffer(256)
    U.GetClassNameW(hwnd, text, len(text))
    return text.value


def windows(pid, parent=None):
    found = []
    @CALLBACK
    def callback(hwnd, unused):
        owner = W.DWORD()
        U.GetWindowThreadProcessId(hwnd, C.byref(owner))
        if owner.value == pid:
            found.append(hwnd)
        return True
    if parent:
        U.EnumChildWindows(parent, callback, 0)
    else:
        U.EnumWindows(callback, 0)
    return found


def wait_for(function, timeout=8):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        value = function()
        if value:
            return value
        time.sleep(.05)
    raise AssertionError('Timed out waiting for the owned player UI')


class TreeItem(C.Structure):
    _fields_ = [('mask', W.UINT), ('item', W.HANDLE), ('state', W.UINT),
                ('state_mask', W.UINT), ('text', C.c_void_p), ('capacity', C.c_int),
                ('image', C.c_int), ('selected_image', C.c_int),
                ('children', C.c_int), ('data', C.c_ssize_t)]


def tree_paths(pid, tree):
    # TVM_GETITEM requires the text buffer to belong to the control's process.
    process = K.OpenProcess(0x38, False, pid)
    memory = K.VirtualAllocEx(process, None, 4096, 0x3000, 4)
    assert process and memory
    paths = []
    def visit(item, parents):
        while item:
            request = TreeItem(mask=1, item=item, text=memory + 128, capacity=1024)
            assert K.WriteProcessMemory(process, memory, C.byref(request), C.sizeof(request), None)
            assert U.SendMessageW(tree, 0x113E, 0, memory)
            text = C.create_unicode_buffer(1024)
            assert K.ReadProcessMemory(process, memory + 128, text, C.sizeof(text), None)
            path = parents + (text.value,)
            paths.append(path)
            visit(U.SendMessageW(tree, 0x110A, 4, item), path)
            item = U.SendMessageW(tree, 0x110A, 1, item)
    try:
        visit(U.SendMessageW(tree, 0x110A, 0, 0), ())
        return paths
    finally:
        K.VirtualFreeEx(process, memory, 0, 0x8000)
        K.CloseHandle(process)


class ReplayBrowserUiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'validation' / 'replay_browser' / ('ui_' + uuid.uuid4().hex[:8])
        cls.client = cls.folder / '客户端'
        (cls.client / 'ImagePacks2').mkdir(parents=True)
        # Two empty-state scenes make resource-free valid REP fixtures.
        replay = pack_replay(1.7, {0: struct.pack('<I', 17)},
                             [(0, (0,), b''), (400, (0,), b'')])
        cls.paths = ('SkillReplay/Swordman/AUnknown.rep', 'SkillReplay/Swordman/BloodSword.rep',
                     'SkillReplay/Swordman/BloodyRave.rep', 'SkillReplay/Priest/DivinePunishment.rep',
                     'Dungeon/Swordman/BloodyRave.rep', 'Dungeon/Nested/中文.REP',
                     'RootReplay.rep', '剧情/第一幕/Opening.rep')
        for relative in cls.paths:
            path = cls.client / 'Replay' / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(replay)
        cls.external = cls.folder / '列表外 录像.rep'
        cls.external.write_bytes(replay)

    def setUp(self):
        self.process = subprocess.Popen([str(ROOT / 'build' / 'rep_player.exe'),
                                         '--client', str(self.client)], cwd=ROOT)
        self.window = wait_for(lambda: next((w for w in windows(self.process.pid)
                                             if class_name(w) == 'NativeRepPlayer'), None))

    def tearDown(self):
        U.PostMessageW(self.window, 0x10, 0, 0)
        try:
            self.process.wait(timeout=6)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            self.process.wait(timeout=3)

    def test_tree_preserves_all_replay_directories_and_skill_labels(self):
        tree = next(w for w in windows(self.process.pid, self.window)
                    if class_name(w) == 'SysTreeView32')
        paths = tree_paths(self.process.pid, tree)
        self.assertIn(('Dungeon', 'Nested', '中文.REP'), paths)
        self.assertIn(('Dungeon', 'Swordman', 'BloodyRave.rep'), paths)
        self.assertIn(('RootReplay.rep',), paths)
        self.assertIn(('剧情', '第一幕', 'Opening.rep'), paths)
        self.assertIn(('SkillReplay', '鬼剑士', '嗜魂封魔斩'), paths)

    def test_open_rep_dialog_plays_external_file_and_cancel_preserves_it(self):
        button = U.GetDlgItem(self.window, 21)
        self.assertTrue(button, 'Open REP button is missing')
        U.PostMessageW(self.window, 0x111, 21, 0)
        dialog = wait_for(lambda: next((w for w in windows(self.process.pid)
                                       if class_name(w) == '#32770'), None))
        edits = [w for w in windows(self.process.pid, dialog) if class_name(w) == 'Edit']
        filename = next((w for w in edits if U.GetDlgCtrlID(w) == 1001), None)
        self.assertTrue(filename, 'REP dialog filename edit is missing')
        value = C.create_unicode_buffer(str(self.external))
        U.SendMessageW(filename, 0xC, 0, C.cast(value, C.c_void_p).value)
        U.PostMessageW(dialog, 0x111, 1, 0)
        wait_for(lambda: not U.IsWindow(dialog))
        wait_for(lambda: any(self.external.name in label(w)
                             for w in windows(self.process.pid, self.window)))
        wait_for(lambda: '播放结束' in label(U.GetDlgItem(self.window, 6)))
        U.PostMessageW(self.window, 0x111, 21, 0)
        dialog = wait_for(lambda: next((w for w in windows(self.process.pid)
                                       if class_name(w) == '#32770'), None))
        U.PostMessageW(dialog, 0x111, 2, 0)
        wait_for(lambda: not U.IsWindow(dialog))
        self.assertTrue(any(self.external.name in label(w)
                            for w in windows(self.process.pid, self.window)))


if __name__ == '__main__':
    unittest.main()
