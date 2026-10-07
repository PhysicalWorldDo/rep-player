"""Sound controls exercised in an owned copy of the actual native window."""
import json
import time
import unittest

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
        self.user.PostMessageW(popup, 0x0010, 0, 0)
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


if __name__ == '__main__':
    unittest.main()
