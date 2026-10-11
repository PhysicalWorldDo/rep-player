"""Persistent category switches exercised in owned native windows and exports."""
import ctypes as C
from ctypes import wintypes as W
from contextlib import contextmanager
import struct
import time
import unittest

from PIL import Image

from test_navigation_ui import OwnedPlayerUi, canvas_ui


class ListItem(C.Structure):
    _fields_ = [('mask', W.UINT), ('item', C.c_int), ('subitem', C.c_int),
                ('state', W.UINT), ('state_mask', W.UINT), ('text', C.c_void_p),
                ('text_length', C.c_int), ('image', C.c_int), ('parameter', W.LPARAM),
                ('indent', C.c_int)]


class NativeImageFilterSwitchUiTests(OwnedPlayerUi):
    suite_name = 'image_filter_switches_ui_20261011'

    def setUp(self):
        super().setUp()
        self.samples = (
            ('sprite/background/tile.img', (255, 0, 0, 255), (1, 1)),
            ('sprite/monster/test/body.img', (0, 255, 0, 255), (6, 1)),
            ('sprite/character/test/body.img', (0, 0, 255, 255), (11, 1)),
            ('sprite/character/test/effect/glow.img', (255, 255, 0, 255), (1, 6)),
            ('sprite/monster/test/effects/glow.img', (255, 0, 255, 255), (6, 6)),
        )
        for path, rgba, unused in self.samples:
            raw = bytes((rgba[2], rgba[1], rgba[0], rgba[3])) * 16
            image = canvas_ui.img_header(2, 36, 1) + canvas_ui.rec(16, 5, 4, 4, 64, x=0, y=0, full_w=4, full_h=4) + raw
            pack_name = path.rsplit('/', 1)[0].replace('/', '_') + '.NPK'
            (self.client / 'ImagePacks2' / pack_name).write_bytes(canvas_ui.npk(image, path))
        self.write_category_replay()
        self.other_replay = self.client / 'Replay' / 'other.rep'
        self.other_replay.write_bytes(self.replay.read_bytes())
        self.settings = self.folder / 'runtime' / 'image_filters.txt'

    def write_category_replay(self, duration=100):
        commands, auxiliary = {}, bytearray()
        for index, (path, color, position) in enumerate(self.samples):
            params = bytearray(canvas_ui._default_draw_params(b''))
            struct.pack_into('<I', params, 0, index)
            struct.pack_into('<2f', params, 28, 0, 0)
            commands[index] = struct.pack('<II', 3, 64) + params
            auxiliary += struct.pack('<2h', *position)
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        self.replay.write_bytes(canvas_ui.pack_replay(1.7, commands,
            [(0, tuple(commands), auxiliary), (duration, tuple(commands), auxiliary)],
            [item[0] for item in self.samples], header=header))
        self.record_count = 2

    def start_player(self, ended=True):
        super().start_player(ended)
        self.command(self.main, 13)  # All IMG, including future records.
        self.wait(lambda: self.user.SendMessageW(self.image_list(), 0x1004, 0, 0) == len(self.samples), 'all fixture IMG entries')

    def image_list(self):
        return self.control(self.control(self.main, 1011), 15)

    @contextmanager
    def list_buffer(self, data=b'', capacity=4096):
        # Common controls require remote buffers for LVM_* pointer arguments.
        # Open only this fixture's PID; no code is injected or executed remotely.
        kernel = C.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
        kernel.OpenProcess.restype = W.HANDLE
        kernel.VirtualAllocEx.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD, W.DWORD]
        kernel.VirtualAllocEx.restype = C.c_void_p
        kernel.VirtualFreeEx.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD]
        kernel.WriteProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
        kernel.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
        kernel.CloseHandle.argtypes = [W.HANDLE]
        process = kernel.OpenProcess(0x0038, False, self.process.pid)
        self.assertTrue(process, 'owned player common-control buffer access')
        remote = kernel.VirtualAllocEx(process, None, capacity, 0x3000, 4)
        try:
            self.assertTrue(remote)
            if data:
                source = C.create_string_buffer(data)
                self.assertTrue(kernel.WriteProcessMemory(process, remote, source, len(data), None))
            yield kernel, process, remote
        finally:
            if remote:
                kernel.VirtualFreeEx(process, remote, 0, 0x8000)
            kernel.CloseHandle(process)

    def image_paths(self):
        result = []
        count = self.user.SendMessageW(self.image_list(), 0x1004, 0, 0)
        with self.list_buffer(capacity=8192) as (kernel, process, remote):
            for index in range(count):
                item = ListItem()
                item.item, item.text, item.text_length = index, remote + 128, 2048
                self.assertTrue(kernel.WriteProcessMemory(process, remote, C.byref(item), C.sizeof(item), None))
                self.user.SendMessageW(self.image_list(), 0x1073, index, remote)  # LVM_GETITEMTEXTW
                text = C.create_unicode_buffer(2048)
                self.assertTrue(kernel.ReadProcessMemory(process, remote + 128, text, C.sizeof(text), None))
                result.append(text.value)
        return result

    def checked(self, path):
        index = self.image_paths().index(path)
        return self.user.SendMessageW(self.image_list(), 0x102C, index, 0xF000) == 0x2000

    def manual(self, path, visible):
        index = self.image_paths().index(path)
        item = ListItem()
        item.state, item.state_mask = (2 if visible else 1) << 12, 0xF000
        data = C.string_at(C.byref(item), C.sizeof(item))
        with self.list_buffer(data) as (kernel, process, remote):
            self.assertTrue(self.user.SendMessageW(self.image_list(), 0x102B, index, remote))  # LVM_SETITEMSTATE -> LVN_ITEMCHANGED
        self.wait(lambda: self.checked(path) == visible, 'manual checkbox visibility')

    def click_selected_replay(self):
        tree = self.control(self.control(self.main, 1010), 4)
        leaf = self.user.SendMessageW(tree, 0x110A, 9, 0)  # TVGN_CARET
        self.assertTrue(leaf)
        with self.list_buffer(struct.pack('<Q', leaf)) as (kernel, process, remote):
            self.assertTrue(self.user.SendMessageW(tree, 0x1104, 0, remote))  # TVM_GETITEMRECT
            rectangle = W.RECT()
            self.assertTrue(kernel.ReadProcessMemory(process, remote, C.byref(rectangle), C.sizeof(rectangle), None))
        point = 40 | (((rectangle.top + rectangle.bottom) // 2) << 16)
        self.user.SendMessageW(tree, 0x0201, 1, point)
        self.user.SendMessageW(tree, 0x0202, 0, point)

    def toggle_filter(self, identifier, enabled):
        self.command(self.main, identifier)
        self.wait(lambda: self.text(self.control(self.main, identifier)).endswith(('开', 'On') if enabled else ('关', 'Off')), 'category switch state')
        expected = ['1' if self.text(self.control(self.main, item)).endswith(('开', 'On')) else '0' for item in (18, 19, 20)]
        self.wait(lambda: self.settings.is_file() and self.settings.read_text().split() == expected, 'saved category switches')

    def export_pixels(self, name, during=None):
        self.wait(lambda: self.user.IsWindowEnabled(self.control(self.main, 11)), 'export available')
        self.command(self.main, 11)
        dialog = self.wait(lambda: next((w for w, kind in self.windows() if kind == 'RepExportSettings'), None), 'export dialog')
        selector = self.control(dialog, 101)
        self.user.SendMessageW(selector, 0x014E, 2, 0)
        self.user.SendMessageW(dialog, 0x0111, 101 | (1 << 16), selector)
        self.user.SendMessageW(self.control(dialog, 102), 0x014E, 1, 0)  # 30 FPS
        self.edit(dialog, 106, name)
        self.user.SendMessageW(dialog, 0x0111, 1, 0)
        self.wait(lambda: not self.user.IsWindowVisible(dialog), 'export accepted')
        self.user.EnableWindow(self.main, False)
        if during:
            self.wait(lambda: not self.user.IsWindowEnabled(self.control(self.main, 11)), 'running export before snapshot mutation')
            self.snapshots.append({'export_accepted': name,
                                   'export_button_enabled': bool(self.user.IsWindowEnabled(self.control(self.main, 11))),
                                   'status': self.text(self.control(self.main, 6))})
            self.assertTrue(any(label in self.text(self.control(self.main, 6))
                                for label in ('正在导出', '正在准备导出', 'Exporting', 'Preparing export')),
                            'snapshot mutation must occur during an observable running export')
            during()
        target = self.folder / 'exports' / name
        self.wait(lambda: target.is_dir() and self.user.IsWindowEnabled(self.control(self.main, 11)), 'PNG export completion', timeout=30)
        frames = sorted(target.glob('frame_*.png'))
        self.assertTrue(frames)
        result = []
        for frame in frames:
            with Image.open(frame) as image:
                self.assertEqual(image.size, (16, 16))
                rgba = image.convert('RGBA')
                result.append([rgba.getpixel((position[0] + 1, position[1] + 1)) for path, color, position in self.samples])
        self.snapshots.append({'export': name, 'frames': len(frames), 'first_samples': result[0]})
        return result

    def assert_export_visibility(self, pixels, visible, background_color=None):
        expected = [color if shown else (0, 0, 0, 0) for (path, color, position), shown in zip(self.samples, visible)]
        if background_color is not None and visible[0]:
            expected[0] = background_color
        self.assertTrue(all(frame == expected for frame in pixels), 'actual exported pixels must follow category switches and manual exceptions')

    def assert_preview_visibility(self, name, visible):
        def matches():
            image = self.capture_control(self.control(self.main, 5), name)
            rendered = {color for count, color in image.getcolors(image.width * image.height) if count > 10}
            return all((color[:3] in rendered) == shown
                       for (path, color, position), shown in zip(self.samples, visible))
        self.wait(matches, 'preview pixels follow category switches and manual exceptions')

    def test_category_controls_default_to_off(self):
        self.start_player()
        for identifier, expected in ((18, '移除背景：关'), (19, '移除怪物：关'), (20, '移除人物：关')):
            self.assertEqual(self.text(self.control(self.main, identifier)), expected,
                             'category removal must be a reversible switch with a visible Off state')

    def test_categories_preserve_effects_and_manual_exceptions_until_show_all(self):
        self.start_player()
        self.assert_preview_visibility('default_all', (True,) * 5)
        before = self.frame_number()
        for identifier in (18, 19, 20):
            self.toggle_filter(identifier, True)
        self.assert_preview_visibility('bodies_hidden', (False, False, False, True, True))
        self.wait_phase('Ended')
        self.assertEqual(self.frame_number(), before)
        self.snapshots.append({'after_filter_toggles': self.text(self.control(self.main, 6))})
        self.assert_export_visibility(self.export_pixels('bodies_hidden'), (False, False, False, True, True))
        background = self.samples[0][0]
        effect = self.samples[3][0]
        self.manual(background, True)  # Show an IMG despite an active category.
        self.manual(effect, False)  # Manually hide an otherwise retained effect.
        self.toggle_filter(18, False)
        self.toggle_filter(18, True)
        self.toggle_filter(19, False)
        self.toggle_filter(19, True)
        self.assertTrue(self.checked(background), 'category toggles must preserve a manual visible exception')
        self.assertFalse(self.checked(effect), 'unrelated toggles must preserve a manual hidden exception')
        self.assert_preview_visibility('manual_wins', (True, False, False, False, True))
        self.assert_export_visibility(self.export_pixels('manual_wins'), (True, False, False, False, True))
        self.command(self.main, 26)
        self.wait(lambda: self.text(self.control(self.main, 26)).endswith('开'), 'negative offsets enabled')
        self.command(self.main, 14)
        self.wait(lambda: self.settings.read_text().split() == ['0', '0', '0'], 'Show all clears global category switches')
        self.assertTrue(all(self.checked(path) for path, color, position in self.samples))
        self.assertTrue(self.text(self.control(self.main, 26)).endswith('开'))
        self.assert_preview_visibility('show_all', (True,) * 5)
        self.assert_export_visibility(self.export_pixels('show_all'), (True,) * 5)

    def test_switches_persist_across_refresh_restart_and_rep_switch_but_manual_choices_do_not(self):
        self.write_category_replay(duration=1000)
        self.other_replay.write_bytes(self.replay.read_bytes())
        self.start_player()
        background = self.samples[0][0]
        self.toggle_filter(18, True)
        self.manual(background, True)
        self.click_selected_replay()
        self.wait_phase('Playing')
        self.wait_phase('Ended')
        self.assertTrue(self.checked(background), 'same-REP replay must keep manual exceptions')
        self.user.SendMessageW(self.main, 0x0111, 25, 0)
        self.wait(lambda: self.user.IsWindowEnabled(self.control(self.main, 25)) and self.phase('Ended'), 'NPK refresh complete')
        self.assertTrue(self.checked(background), 'refresh must preserve manual exceptions')
        self.edit(self.main, 8, 'other.rep')
        tree = self.control(self.control(self.main, 1010), 4)
        leaf = self.user.SendMessageW(tree, 0x110A, 0, 0)
        self.assertTrue(leaf)
        self.user.SendMessageW(tree, 0x110B, 9, leaf)
        self.wait_phase('Playing')
        self.wait(lambda: 'other' in self.text(self.main) and self.phase('Ended'), 'different REP selected')
        self.assertTrue(self.text(self.control(self.main, 18)).endswith('开'))
        self.assertFalse(self.checked(background), 'true REP switch clears only manual exceptions')
        self.close_player()
        self.start_player()
        self.assertTrue(self.text(self.control(self.main, 18)).endswith('开'))
        self.assertFalse(self.checked(background))

    def test_filter_changes_do_not_apply_pending_patch_selection_and_export_keeps_its_snapshot(self):
        background = self.samples[0][0]
        blue = (0, 128, 255, 255)
        raw = bytes((blue[2], blue[1], blue[0], blue[3])) * 16
        image = canvas_ui.img_header(2, 36, 1) + canvas_ui.rec(16, 5, 4, 4, 64, x=0, y=0, full_w=4, full_h=4) + raw
        (self.client / 'ImagePacks2' / '!patch.npk').write_bytes(canvas_ui.npk(image, background))
        self.write_category_replay(duration=4000)
        self.start_player()
        self.command(self.main, 27)
        self.wait(lambda: self.user.SendMessageW(self.control(self.main, 27), 0x00F0, 0, 0) == 0, 'pending disable patches')
        self.toggle_filter(18, True)
        pixels = self.export_pixels('snapshot_hidden', during=lambda: self.toggle_filter(18, False))
        self.assert_export_visibility(pixels, (False, True, True, True, True))
        self.assert_export_visibility(self.export_pixels('pending_patch_still_applied'), (True,) * 5, blue)
        self.assertEqual((self.folder / 'runtime' / 'npk_patches.txt').read_text().strip(), '1')
        self.user.SendMessageW(self.main, 0x0111, 25, 0)
        self.wait(lambda: (self.folder / 'runtime' / 'npk_patches.txt').read_text().strip() == '0'
                  and self.user.IsWindowEnabled(self.control(self.main, 25)) and self.phase('Ended'), 'disabled patch index published')
        self.assert_export_visibility(self.export_pixels('refresh_uses_original'), (True,) * 5)

    def test_three_parallel_switches_fit_both_languages_at_minimum_size(self):
        runtime = self.folder / 'runtime'
        runtime.mkdir()
        (runtime / 'layout.txt').write_text('300 0.9', encoding='utf8')
        self.start_player()
        self.user.SetWindowPos(self.main, None, 0, 0, 1040, 720, 0x0002 | 0x0004)
        for locale, labels in ((9, ('移除背景：关', '移除怪物：关', '移除人物：关')),
                               (10, ('Hide BG: Off', 'Hide monsters: Off', 'Hide people: Off'))):
            self.command(self.main, locale)
            self.wait(lambda: self.text(self.control(self.main, 18)) == labels[0], 'category translations')
            self.capture_control(self.main, 'category_minimum_' + str(locale))
            rectangles = [self.rect(self.control(self.main, identifier)) for identifier in (18, 19, 20)]
            for identifier, label, rectangle in zip((18, 19, 20), labels, rectangles):
                self.assertEqual(self.text(self.control(self.main, identifier)), label)
                self.assertGreaterEqual(rectangle.bottom - rectangle.top, 36)
                self.assert_button_text_fits(self.control(self.main, identifier), label, rectangle.right - rectangle.left)
            for previous, following in zip(rectangles, rectangles[1:]):
                self.assertLessEqual(previous.right, following.left)
                self.assertEqual(previous.top, following.top)
                self.assertEqual(previous.bottom, following.bottom)
            self.assertLessEqual(rectangles[-1].bottom, self.rect(self.control(self.main, 26)).top)
            host = self.rect(self.control(self.main, 1011))
            self.assertGreaterEqual(host.bottom - host.top, 36)

    def assert_button_text_fits(self, button, label, width):
        # HFONT belongs to the player process. It cannot be selected into a DC
        # owned by this Python process; measure the real rendered pixels instead.
        self.user.GetDC.argtypes = [W.HWND]
        self.user.GetDC.restype = W.HDC
        self.user.ReleaseDC.argtypes = [W.HWND, W.HDC]
        gdi = C.WinDLL('gdi32', use_last_error=True)
        gdi.SelectObject.argtypes = [W.HDC, C.c_void_p]
        gdi.SelectObject.restype = C.c_void_p
        dc = self.user.GetDC(button)
        self.assertTrue(dc)
        font = self.user.SendMessageW(button, 0x0031, 0, 0)  # WM_GETFONT
        previous = gdi.SelectObject(dc, font)
        try:
            self.snapshots.append({'font_probe': label, 'foreign_hfont': int(font),
                                   'foreign_select_succeeded': bool(previous)})
        finally:
            if previous:
                gdi.SelectObject(dc, previous)
            self.user.ReleaseDC(button, dc)
        name = 'category_button_' + str(button) + ('_en' if label.isascii() else '_zh')
        image = self.capture_control(button, name)
        self.assertEqual(image.width, width)
        for row, line in enumerate(label.replace('：', ':').split(':')):
            first = row * (image.height // 2)
            last = image.height if row else image.height // 2
            xs = [x for y in range(first, last) for x in range(image.width)
                  if sum(image.getpixel((x, y))) > 420]
            self.assertTrue(xs, 'both category name and switch state must be visibly rendered')
            left, right = min(xs), image.width - max(xs) - 1
            self.snapshots.append({'button_line': line.strip(), 'rendered_ink_width': max(xs) - min(xs) + 1,
                                   'left_margin_pixels': left, 'right_margin_pixels': right})
            self.assertGreaterEqual(left, 3, repr(line.strip()) + ' needs a visible left margin')
            self.assertGreaterEqual(right, 3, repr(line.strip()) + ' needs a visible right margin')

    def test_invalid_saved_switches_default_off(self):
        runtime = self.folder / 'runtime'
        runtime.mkdir()
        for malformed in ('1 1 2', '1 0', '1 0 0 extra', 'invalid'):
            self.settings.write_text(malformed, encoding='utf8')
            self.start_player()
            self.assertTrue(all(self.text(self.control(self.main, item)).endswith('关') for item in (18, 19, 20)))
            self.close_player()


if __name__ == '__main__':
    unittest.main()
