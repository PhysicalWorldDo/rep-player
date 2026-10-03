"""Inspect the built PE's Windows icon and version resources (requires pefile)."""
import argparse
import json
from pathlib import Path
import pefile

parser = argparse.ArgumentParser()
parser.add_argument('exe', type=Path)
parser.add_argument('--report', type=Path)
args = parser.parse_args()
pe = pefile.PE(str(args.exe))
entries = getattr(getattr(pe, 'DIRECTORY_ENTRY_RESOURCE', None), 'entries', [])
types = {entry.id: entry for entry in entries}
groups = types.get(14)  # RT_GROUP_ICON
sizes = []
if groups:
    for name in groups.directory.entries:
        data = name.directory.entries[0].data.struct
        raw = pe.get_data(data.OffsetToData, data.Size)
        count = int.from_bytes(raw[4:6], 'little')
        for index in range(count):
            width, height = raw[6 + index * 14:8 + index * 14]
            sizes.append([width or 256, height or 256])
report = {'exe': str(args.exe.resolve()), 'icon_sizes': sizes,
          'icon_present': bool(groups and types.get(3)), 'version_present': 16 in types}
report['pass'] = report['icon_present'] and report['version_present'] and all(
    [size, size] in sizes for size in (16, 24, 32, 48, 64, 128, 256))
if args.report:
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
print(json.dumps(report, ensure_ascii=False))
raise SystemExit(0 if report['pass'] else 1)
