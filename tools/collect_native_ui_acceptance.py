"""Collect this approved native UI/export revision's existing test evidence."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FOLDER = ROOT / 'validation' / 'native_ui_v2'


def read(relative):
    return json.loads((ROOT / relative).read_text(encoding='utf-8-sig'))


def main():
    full_log = (FOLDER / 'unittest_final.log').read_text(encoding='utf-8-sig')
    assert 'Ran 59 tests' in full_log and full_log.rstrip().endswith('OK')
    feature = read('validation/ui_features.json')
    client_feature = read('validation/native_ui_v2/client_features.json')
    benchmark = read('validation/native_ui_v2/ui_benchmark.json')
    representatives = read('validation/representatives/summary.json')
    shaders = read('validation/shader_pixel_differential.json')
    exports = read('validation/new_ui_export/bloodyrave_20261003_122019/summary.json')
    laus = read('validation/native_ui_v2/laus_alternate_client.json')
    assert all(feature.values()) and all(client_feature.values()) and benchmark['pass']
    assert representatives['passed'] == 40 and shaders['passed'] == 1068 and exports['passed']
    assert laus['scenes'] == 1119 and laus['exact_eof'] and laus['fallbacks'] == 0
    result = {
        'revision': 'Native bilingual right sidebar and real material export, 2026-10-03',
        'executable': str(ROOT / 'build/rep_player.exe'),
        'start': str(ROOT / 'Start.cmd'),
        'build': 'build.ps1 compiled/linked successfully with C++20 D3D11',
        'full_unittest': {'passed': 59, 'log': str(FOLDER / 'unittest_final.log')},
        'focused_ui_logs': ['validation/native_ui_v2/ui_final.log', 'validation/native_ui_v2/ui_feature_final.log'],
        'ui_feature_checks': feature,
        'alternate_client': str(ROOT / 'runtime/alternate_client'),
        'alternate_client_feature_checks': client_feature,
        'catalog': {'total': 3064, 'exact_matches': 2878, 'unmatched_original_filenames': 186},
        'representatives': {k: representatives[k] for k in ('files', 'passed', 'scenes', 'seconds')},
        'shader_pixels': {k: shaders[k] for k in ('cases', 'passed', 'seconds', 'acceptance')},
        'laus_alternate_client': laus,
        'real_exports': exports,
        'benchmark': benchmark,
        'source_review': 'Fresh independent reviewer: ready, no unresolved Critical/Important',
        'desktop_control_evidence': ['directory_dialog_accessibility.txt', 'keyboard_language_accessibility.txt', 'final_window_accessibility.txt'],
        'desktop_verification': {
            'native_control_tree_read': True,
            'english_filename_search_observed': True,
            'focused_language_button_space_activation_observed': True,
            'language_switch_retained_search_and_ended_replay_observed': True,
            'final_chinese_window_3064_entries_33_current_img_paths_and_last_frame_observed': True,
            'native_folder_dialog_opened_by_keyboard': True,
            'full_folder_selection_manual_check': False,
            'export_dialog_manual_check': False,
            'visual_layout_screenshot_verified': False,
            'limitation': 'sky window capture timed out; owned IFileDialog input indexes unavailable in cached app state; coordinate input geometry unavailable',
        },
        'material_limits': [
            'Whole timeline includes the last frame, adding less than two output frames to recorded duration.',
            'Ordinary RGBA cannot carry additive/multiply blend operators; export coverage preserves visible color energy over black.',
            'Outputs are constrained to rep_player for this task; same-name files are not overwritten; partial failures remain marked .partial.',
        ],
    }
    path = FOLDER / 'acceptance.json'
    path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(path)


if __name__ == '__main__':
    main()
