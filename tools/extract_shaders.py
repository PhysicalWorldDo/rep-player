"""Export the client's original DXBC without running the client or writing IDA."""
import ctypes
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
OLD = Path(r'D:\DNF115us\skill_player')
OUT = ROOT / 'assets' / 'shaders'
OUT.mkdir(parents=True, exist_ok=True)
exe = Path(r'D:\115us\client\DFO.exe').read_bytes()
pe = struct.unpack_from('<I', exe, 60)[0]
count, optsize = struct.unpack_from('<H12xH', exe, pe + 6)
base = struct.unpack_from('<Q', exe, pe + 24 + 24)[0]
sections = []
for n in range(count):
    off = pe + 24 + optsize + n * 40
    vs, va, rs, rp = struct.unpack_from('<4I', exe, off + 8)
    sections.append((va, max(vs, rs), rp))

def readva(va, size):
    for start, length, raw in sections:
        if start <= va - base < start + length:
            at = raw + va - base - start
            return exe[at:at + size]
    raise ValueError(hex(va))

compiler = ctypes.WinDLL('d3dcompiler_47.dll')
disasm = compiler.D3DDisassemble
disasm.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
disasm.restype = ctypes.c_long

def assembly(raw):
    buffer = ctypes.create_string_buffer(raw)
    out = ctypes.c_void_p()
    hr = disasm(buffer, len(raw), 0, None, ctypes.byref(out))
    if hr < 0:
        raise ValueError(f'D3DDisassemble: {hr:#x}')
    vt = ctypes.cast(out, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
    ptr = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vt[3])(out)
    length = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vt[4])(out)
    text = ctypes.string_at(ptr, length).decode('utf8').rstrip('\0')
    ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(vt[2])(out)
    return text

def shader_code(text, kind):
    match = re.search(r'\b' + kind + r'_\d_\d\b(.*?)// Approximately', text, re.S)
    if not match:
        return None
    return '\n'.join(line.strip() for line in match.group(0).splitlines()[:-1])

registry = json.loads((OLD / 'shader_registry_index.json').read_text(encoding='utf8'))
confirmed = {}
confirmed_sizes = {}
for filename in ('shader_native_evidence.json', 'shader_complex_fxo.json', 'shader_static_fxo.json',
                 'shader_legal_simple_fxo.json', 'shader_legal_simple_observed_fxo.json',
                 'shader_legal_a_fxo.json', 'shader_legal_b_fxo.json', 'shader_legal_c_fxo.json'):
    data = json.loads((OLD / filename).read_text(encoding='utf8'))
    records = data if isinstance(data, list) else data.get('full_fxo_simple', data.get('fxo', [data]))
    for record in records:
        blob = record.get('native_blob', record.get('blob'))
        if blob is None or 'assembly' not in record:
            continue
        address = int(blob, 16) if isinstance(blob, str) else blob
        confirmed_sizes[address] = record.get('size', record.get('length', struct.unpack('<Q', readva(address - 8, 8))[0]))
        for name in re.findall(r'technique11\s+(\w+)', record['assembly']):
            confirmed[name] = address
manifest, blobs = [], {}
for row in registry:
    if 'blob' not in row:
        continue
    va = int(row['blob'], 16) if isinstance(row['blob'], str) else row['blob']
    va = confirmed.get(row['technique'], va)
    if va not in blobs:
        size = confirmed_sizes[va] if va in confirmed_sizes else struct.unpack('<Q', readva(row['size_address'], 8))[0]
        raw = readva(va, size)
        try:
            text = assembly(raw)
        except ValueError as exc:
            raise ValueError((hex(va), size, readva(va, 32).hex())) from exc
        shaders = []
        offset = 4
        while True:
            offset = raw.find(b'DXBC', offset)
            if offset < 0:
                break
            length = struct.unpack_from('<I', raw, offset + 24)[0]
            bytecode = raw[offset:offset + length]
            try:
                asm = assembly(bytecode)
                for kind in ('vs', 'ps'):
                    code = shader_code(asm, kind)
                    if code:
                        name = hashlib.sha256(bytecode).hexdigest()[:24] + '.' + kind + '.dxbc'
                        (OUT / name).write_bytes(bytecode)
                        (OUT / (name + '.asm')).write_text(asm, encoding='utf8')
                        shaders.append((kind, code, name, asm))
            except ValueError:
                pass
            offset += 4
        (OUT / f'fxo_{va:x}.bin').write_bytes(raw)
        blobs[va] = text, shaders
    text, shaders = blobs[va]
    name = row['technique']
    match = re.search(r'technique11\s+' + re.escape(name) + r'\b(.*?)(?=technique11|\Z)', text, re.S)
    if not match:
        continue
    body = match.group(1)
    record = dict(row)
    record['selected_blob'] = hex(va)
    for kind in ('vs', 'ps'):
        target = shader_code(body, kind)
        matches = [x for x in shaders if x[0] == kind and x[1] == target]
        if not matches:
            raise ValueError(f'missing original {kind}: {name}, candidates {len(shaders)}')
        record[kind] = matches[0][2]
    states = {}
    for sampler, state in re.findall(r'SamplerState (\w+)\s*\{(.*?)\}', text, re.S):
        filtering = re.search(r'Filter\s*=\s*uint\((\w+)', state)
        addressing = re.findall(r'Address[UV]\s*=\s*uint\((\w+)', state)
        states[sampler] = ('point' if filtering and filtering.group(1) == 'MIN_MAG_MIP_POINT' else 'linear',
                           addressing[0].lower() if addressing else 'clamp')
    psasm = next(x[3] for x in shaders if x[2] == record['ps'])
    bindings = re.findall(r'//\s*(\w+)\s+sampler\s+NA\s+NA\s+(s\d+)\s+\d+', psasm)
    record['samplers'] = {slot: states.get(sampler, ('linear', 'clamp')) for sampler, slot in bindings}
    manifest.append(record)
(OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf8')
lines = ['#pragma once', 'struct ShaderRecord { int id; const char *name,*vs,*ps; unsigned pointMask,mirrorMask,borderMask; };', 'inline constexpr ShaderRecord shaderRecords[] = {']
for row in manifest:
    masks = [0, 0, 0]
    for slot, (filtering, address) in row['samplers'].items():
        index = int(slot[1:])
        if filtering == 'point': masks[0] |= 1 << index
        if address == 'mirror': masks[1] |= 1 << index
        if address == 'border': masks[2] |= 1 << index
    lines.append('{%d,%s,%s,%s,%du,%du,%du},' % (row['program_id'], json.dumps(row['technique']), json.dumps(row['vs']), json.dumps(row['ps']), *masks))
lines.append('};')
(ROOT / 'src' / 'shader_registry.hpp').write_text('\n'.join(lines) + '\n', encoding='utf8')
print(json.dumps({'registered_programs': len(manifest), 'fxo_blobs': len(blobs), 'original_dxbc': len(list(OUT.glob('*.dxbc')))}, indent=2))
