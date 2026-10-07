"""Sound controls exercised in an owned copy of the actual native window."""
import json
import struct
import time
import unittest
from PIL import Image

import test_canvas_ui as canvas_ui


class NativeAudioUiTests(canvas_ui.NativeCanvasUiTests):
    test_three_modes_drafts_apply_cancel_persistence_and_export_entry = None
    test_numeric_edit_space_does_not_toggle_paused_playback = None

    def close_player(self):
        if self.process and self.process.poll() is None:
            for handle, kind in self.windows():
                if kind == 'RepAudioVolume':
                    self.user.PostMessageW(handle, 0x0010, 0, 0)
        super().close_player()

    def open_volume(self):
        self.command(self.main, 24)
        return self.wait(lambda: next((w for w, kind in self.windows() if kind == 'RepAudioVolume'), None), 'volume popup')

    def capture_control(self, handle, name):
        C, W = canvas_ui.C, canvas_ui.W
        gdi = C.WinDLL('gdi32', use_last_error=True)
        self.user.GetWindowDC.argtypes = [W.HWND]
        self.user.GetWindowDC.restype = W.HDC
        self.user.ReleaseDC.argtypes = [W.HWND, W.HDC]
        self.user.PrintWindow.argtypes = [W.HWND, W.HDC, W.UINT]
        gdi.CreateCompatibleDC.argtypes = [W.HDC]
        gdi.CreateCompatibleDC.restype = W.HDC
        gdi.CreateCompatibleBitmap.argtypes = [W.HDC, canvas_ui.C.c_int, C.c_int]
        gdi.CreateCompatibleBitmap.restype = W.HBITMAP
        gdi.SelectObject.argtypes = [W.HDC, C.c_void_p]
        gdi.SelectObject.restype = C.c_void_p
        gdi.DeleteObject.argtypes = [C.c_void_p]
        gdi.DeleteDC.argtypes = [W.HDC]
        class Header(C.Structure):
            _fields_ = [('size', W.DWORD), ('width', W.LONG), ('height', W.LONG), ('planes', W.WORD),
                        ('bits', W.WORD), ('compression', W.DWORD), ('image_bytes', W.DWORD),
                        ('x_resolution', W.LONG), ('y_resolution', W.LONG), ('colors', W.DWORD), ('important', W.DWORD)]
        class Info(C.Structure):
            _fields_ = [('header', Header), ('colors', W.DWORD * 3)]
        gdi.GetDIBits.argtypes = [W.HDC, W.HBITMAP, W.UINT, W.UINT, C.c_void_p, C.POINTER(Info), W.UINT]
        rectangle = self.rect(handle)
        width, height = rectangle.right - rectangle.left, rectangle.bottom - rectangle.top
        window_dc = self.user.GetWindowDC(handle)
        memory_dc = gdi.CreateCompatibleDC(window_dc)
        bitmap = gdi.CreateCompatibleBitmap(window_dc, width, height)
        old = gdi.SelectObject(memory_dc, bitmap)
        try:
            self.assertTrue(self.user.PrintWindow(handle, memory_dc, 2), 'owned sound button PrintWindow failed')
            info = Info()
            info.header = Header(C.sizeof(Header), width, -height, 1, 32, 0, width * height * 4, 0, 0, 0, 0)
            pixels = C.create_string_buffer(width * height * 4)
            self.assertEqual(gdi.GetDIBits(memory_dc, bitmap, 0, height, pixels, C.byref(info), 0), height)
            image = Image.frombytes('RGB', (width, height), pixels.raw, 'raw', 'BGRX')
            image.save(self.folder / (name + '.png'))
            return image
        finally:
            gdi.SelectObject(memory_dc, old)
            gdi.DeleteObject(bitmap)
            gdi.DeleteDC(memory_dc)
            self.user.ReleaseDC(handle, window_dc)

    def test_mute_volume_persist_and_fit_the_minimum_window(self):
        self.start_player()
        mute, volume = (self.control(self.main, i) for i in (23, 24))
        self.assertTrue(mute and volume, 'authorized mute and volume controls are missing')
        self.user.SetWindowPos(self.main, None, 0, 0, 1040, 720, 0x0002 | 0x0004)
        time.sleep(.1)
        main, muted, vol = self.rect(self.main), self.rect(mute), self.rect(volume)
        self.assertLessEqual(muted.right, vol.left)
        self.assertLessEqual(vol.right, main.right)
        self.assertGreaterEqual(muted.top, self.rect(self.control(self.main, 16)).top - 48)
        popup = self.open_volume()
        slider = self.control(popup, 401)
        self.assertTrue(slider)
        self.assertEqual(self.user.SendMessageW(slider, 0x0400, 0, 0), 100)  # TBM_GETPOS
        self.user.SendMessageW(slider, 0x0405, 1, 25)  # TBM_SETPOS
        self.user.SendMessageW(popup, 0x0114, 5, slider)  # WM_HSCROLL / TB_THUMBTRACK
        settings = self.folder / 'runtime' / 'audio.txt'
        self.wait(lambda: settings.is_file() and settings.read_text().split() == ['25', '0'], 'saved preview volume')
        self.assertIn('25', self.text(self.control(popup, 402)))
        self.user.PostMessageW(slider, 0x0100, 0x1B, 1)
        self.wait(lambda: not self.user.IsWindowVisible(popup), 'closed volume popup')
        self.command(self.main, 23)
        self.wait(lambda: settings.read_text().split() == ['25', '1'], 'saved mute state')
        self.assertEqual(self.text(mute), '🔇')
        self.close_player()
        self.start_player()
        self.assertEqual(self.text(self.control(self.main, 23)), '🔇')
        popup = self.open_volume()
        self.assertEqual(self.user.SendMessageW(self.control(popup, 401), 0x0400, 0, 0), 25)
        self.snapshots.append({'audio_settings': settings.read_text(), 'mute': self.text(self.control(self.main, 23))})

    def test_export_audio_checkbox_is_default_on_and_png_has_no_audio(self):
        self.start_player()
        self.command(self.main, 11)
        dialog = self.wait(lambda: next((w for w, kind in self.windows() if kind == 'RepExportSettings'), None), 'export dialog')
        checkbox = self.control(dialog, 109)
        self.assertTrue(checkbox, 'export audio choice is missing')
        self.assertTrue(self.user.IsWindowEnabled(checkbox))
        self.assertEqual(self.user.SendMessageW(checkbox, 0x00F0, 0, 0), 1)
        selector = self.control(dialog, 101)
        self.user.SendMessageW(selector, 0x014E, 2, 0)  # CB_SETCURSEL PNG
        self.user.SendMessageW(dialog, 0x0111, 101 | (1 << 16), selector)
        self.assertFalse(self.user.IsWindowEnabled(checkbox))
        self.assertEqual(self.user.SendMessageW(checkbox, 0x00F0, 0, 0), 0)
        self.assertIn('PNG', self.text(self.control(dialog, 110)))
        self.user.SendMessageW(selector, 0x014E, 1, 0)  # MP4
        self.user.SendMessageW(dialog, 0x0111, 101 | (1 << 16), selector)
        self.assertTrue(self.user.IsWindowEnabled(checkbox))
        self.assertEqual(self.user.SendMessageW(checkbox, 0x00F0, 0, 0), 1)
        self.command(dialog, 2)

    def test_missing_recorded_sound_is_visible_in_playback_status(self):
        params = bytearray(canvas_ui._default_draw_params(b''))
        struct.pack_into('<2f', params, 28, 0, 0)
        draw = struct.pack('<II', 3, 64) + params
        sound = struct.pack('<II9i', 6, 36, 1, 0, 0, 1, 1, 0, 0, -1, -1)
        header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
        (self.client / 'audio.xml').write_text('<AUDIO><MUSIC ID="MISSING_SOUND" FILE="music/missing.wav" /></AUDIO>', encoding='utf8')
        self.replay.write_bytes(canvas_ui.pack_replay(1.7, {0: sound, 1: draw},
            [(0, (0, 1), struct.pack('<2h', 2, 3)), (1000, (1,), struct.pack('<2h', 4, 3))],
            ['sprite/test/frame.img', 'MISSING_SOUND'], header=header))
        self.start_player()
        self.wait(lambda: '缺少声音' in self.text(self.control(self.main, 6)) or 'Missing sounds' in self.text(self.control(self.main, 6)), 'visible missing sound diagnostic')
        self.snapshots.append({'status': self.text(self.control(self.main, 6))})

    def test_muted_icon_has_a_visible_speaker_body_and_cone(self):
        self.start_player()
        self.command(self.main, 23)
        self.wait(lambda: self.text(self.control(self.main, 23)) == '🔇', 'muted speaker state')
        image = self.capture_control(self.control(self.main, 23), 'mute_icon')
        x, y = image.width // 2, image.height // 2
        yellow = (234, 208, 91)
        body = sum(image.getpixel((px, py)) == yellow for px in range(x - 10, x - 6) for py in range(y - 3, y + 3))
        cone = sum(image.getpixel((px, py)) == yellow for px in range(x - 6, x + 1) for py in range(y - 6, y + 7))
        self.snapshots.append({'speaker_body_pixels': body, 'speaker_cone_pixels': cone})
        self.assertGreaterEqual(body, 20, 'font fallback draws a circular symbol instead of the speaker body')
        self.assertGreaterEqual(cone, 45, 'the mute icon needs a filled speaker cone')


if __name__ == '__main__':
    unittest.main()
