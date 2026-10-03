"""Verify an unpacked Windows release and launch it without installed client data."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import time
import pefile

parser = argparse.ArgumentParser()
parser.add_argument('package', type=Path)
parser.add_argument('--report', type=Path, required=True)
args = parser.parse_args()
root = args.package.resolve()
system = Path(os.environ['WINDIR']) / 'System32'
binary = root
report = {'package': str(root), 'checks': {}, 'imports': {}}
checks = report['checks']
required = ['rep_player.exe', 'resources/ffmpeg.exe', 'resources/licenses/THIRD_PARTY.txt']
checks['required_files'] = all((root / path).is_file() for path in required)
checks['minimal_root'] = {path.name for path in root.iterdir()} == {'rep_player.exe', 'resources'}
if not checks['required_files']:
    report['pass'] = False
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps({'checks': checks, 'pass': False}))
    raise SystemExit(1)
for exe in (root / 'rep_player.exe', root / 'resources' / 'ffmpeg.exe'):
    imports = []
    queue = [exe]
    visited = set()
    missing = []
    while queue:
        path = queue.pop()
        if path.name.lower() in visited:
            continue
        visited.add(path.name.lower())
        pe = pefile.PE(str(path), fast_load=True)
        pe.parse_data_directories(directories=[1, 13])
        for entry in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []) + getattr(pe, 'DIRECTORY_ENTRY_DELAY_IMPORT', []):
            name = entry.dll.decode('ascii').lower()
            imports.append(name)
            bundled = exe.parent / name
            if bundled.is_file():
                queue.append(bundled)
            elif not (name.startswith(('api-ms-win-', 'ext-ms-win-')) or (system / name).is_file()):
                missing.append(name)
        pe.close()
    report['imports'][exe.name] = {'dependencies': sorted(set(imports)), 'missing': sorted(set(missing))}
checks['dependency_closure'] = all(not item['missing'] for item in report['imports'].values())
checks['no_external_vc_runtime'] = all(not name.startswith(('vcruntime', 'msvcp', 'freetype', 'libpng'))
    for item in report['imports'].values() for name in item['dependencies'])
checks['no_user_data_or_client_assets'] = not any(
    path.suffix.lower() in ('.rep', '.npk', '.img', '.ttf', '.otf', '.pcf', '.bk2', '.avi', '.mp4', '.mov')
    or path.relative_to(root).parts[0].lower() in ('runtime', 'cache', 'exports', 'validation')
    or path.name.lower() in ('dfo.exe', 'dnf.exe', 'bink2w64.dll')
    for path in root.rglob('*') if path.is_file())
player_bytes = (root / 'rep_player.exe').read_bytes()
checks['no_developer_absolute_paths'] = not any(
    value.encode(encoding) in player_bytes
    for value in ('D:\\115us', 'D:\\DNF115us', 'E:\\DNFAutoPlay', 'C:\\Users\\CAO', 'C:\\Windows\\Fonts')
    for encoding in ('utf8', 'utf-16le'))

# The regular GUI must open with an unavailable client path on a fresh installation.
# Read only this process's window; close it normally so no other applications are touched.
user = ctypes.WinDLL('user32', use_last_error=True)
user.EnumWindows.argtypes = [ctypes.c_void_p, wintypes.LPARAM]
user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user.GetClassLongPtrW.argtypes = [wintypes.HWND, ctypes.c_int]
user.GetClassLongPtrW.restype = ctypes.c_size_t
user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user.SendMessageW.restype = wintypes.LPARAM
user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user.EnumChildWindows.argtypes = [wintypes.HWND, ctypes.c_void_p, wintypes.LPARAM]
process = subprocess.Popen([str(binary / 'rep_player.exe')], cwd=system)
window = None
try:
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline and process.poll() is None:
        found = []
        @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        def visit(hwnd, _):
            pid = wintypes.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == process.pid:
                name = ctypes.create_unicode_buffer(128)
                user.GetClassNameW(hwnd, name, len(name))
                if name.value == 'NativeRepPlayer':
                    found.append(hwnd)
            return True
        user.EnumWindows(visit, 0)
        if found:
            window = found[0]
            break
        time.sleep(.1)
    checks['fresh_launch_without_client'] = bool(window)
    checks['window_large_and_small_icons'] = bool(window and user.GetClassLongPtrW(window, -14) and user.GetClassLongPtrW(window, -34))
    if window:
        texts = []
        @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        def child_text(hwnd, _):
            title = ctypes.create_unicode_buffer(1024)
            user.GetWindowTextW(hwnd, title, len(title))
            texts.append(title.value)
            return True
        user.EnumChildWindows(window, child_text, 0)
        checks['first_start_requires_client_choice'] = '请选择客户端目录' in texts or 'Select a client folder' in texts
        user.SendMessageW(window, 0x0010, 0, 0)  # WM_CLOSE
    process.wait(timeout=15)
    checks['normal_gui_shutdown'] = process.returncode == 0
finally:
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=5)
report['pass'] = all(checks.values())
args.report.parent.mkdir(parents=True, exist_ok=True)
args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
print(json.dumps({'checks': checks, 'pass': report['pass']}, ensure_ascii=False))
raise SystemExit(0 if report['pass'] else 1)
