"""Isolated CPU prefetch ownership and bounded-cost CPU/GPU eviction checks."""
from pathlib import Path
import argparse
import json
import struct
import subprocess
import sys
import zlib

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]

def image(value, broken=False, size=128):
    raw = bytes([5, 10, value, 255]) * (size * size)
    compressed = b'broken zlib' if broken else zlib.compress(raw)
    return b'Neople Img File\0' + struct.pack('<4I', 36, 0, 2, 1) + struct.pack('<9i', 16, 6, size, size, len(compressed), 0, 0, size, size) + compressed

def package(data, logical):
    seed = b'puchikon@neople dungeon and fighter '
    while len(seed) < 255:
        seed += b'DNF'
    seed = seed[:255] + b'\0'
    name = (logical.encode() + b'\0').ljust(256, b'\0')
    return b'NeoplePack_Bill\0' + struct.pack('<I', 1) + struct.pack('<2I', 284, len(data)) + bytes(a ^ b for a, b in zip(name, seed)) + data

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', required=True)
    parser.add_argument('--modes', nargs='*')
    args = parser.parse_args()
    folder = Path(args.out).resolve()
    allowed = ROOT / 'validation/load_complete_20261029/resources'
    if not folder.is_relative_to(allowed.resolve()):
        raise RuntimeError('Output must stay within this resource validation directory')
    folder.mkdir(parents=True, exist_ok=True)
    packs = folder / 'packs'
    packs.mkdir(exist_ok=True)
    for n in range(32):
        (packs / f'sprite_cache{n}.NPK').write_bytes(package(image(30+n), f'sprite/cache{n}/frame.img'))
    if not args.modes or 'active-working-set' in args.modes:
        for n in range(33):
            (packs / f'sprite_large{n}.NPK').write_bytes(package(image(30+n,size=1024), f'sprite/large{n}/frame.img'))
    (packs / '000_valid.NPK').write_bytes(package(image(90), 'sprite/valid/frame.img'))
    (packs / 'sprite_valid.NPK').write_bytes(package(image(10), 'sprite/valid/frame.img'))
    (packs / 'sprite_broken.NPK').write_bytes(package(image(20, True), 'sprite/broken/frame.img'))
    compiler = ROOT / 'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
    opts = ['-std=c++20', '-O2', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-I', str(ROOT/'src'), '-I', str(ROOT/'vendor/zlib')]
    instrumented = folder / 'resources_instrumented.cpp'
    source = (ROOT/'src/resources.cpp').read_text(encoding='utf8')
    for target, injected in [
        ('size_t Assets::decodedImageBytes()const{', 'extern void resourceTestCpuAccounting();resourceTestCpuAccounting();'),
        ('std::shared_ptr<Frame> Assets::decodeFrame(const std::string& name,int index,int palette,std::string* source){', 'extern void resourceTestDecodeBarrier(const std::string&);resourceTestDecodeBarrier(name);')]:
        if source.count(target) != 1:
            raise RuntimeError(f'Cannot instrument decoder boundary: {target}')
        source = source.replace(target, target + injected, 1)
    instrumented.write_text(source, encoding='utf8')
    for target, replacement in [
        ('images_[name]=image;', 'resourceTestDecodeBarrier(std::string("publish:")+name);images_[name]=image;'),
        ('auto [it,inserted]=resolvedFrames_.emplace(key,std::move(entry));', 'resourceTestDecodeBarrier(std::string("framepub:")+key.logical);auto [it,inserted]=resolvedFrames_.emplace(key,std::move(entry));')]:
        if source.count(target) != 1:
            raise RuntimeError('Cannot instrument final resource publication')
        source = source.replace(target, replacement, 1)
    source = source.replace('namespace rep {', 'namespace rep { void resourceTestDecodeBarrier(const std::string&);', 1)
    instrumented.write_text(source, encoding='utf8')
    gpu_source = (ROOT/'src/gpu.cpp').read_text(encoding='utf8')
    target = 'size_t Gpu::textureCacheBytes()const{'
    if gpu_source.count(target) != 1:
        raise RuntimeError('Cannot instrument GPU byte accounting')
    gpu_source = gpu_source.replace(target, target + 'extern void resourceTestGpuAccounting();resourceTestGpuAccounting();', 1)
    gpu_instrumented = folder/'gpu_instrumented.cpp'
    gpu_instrumented.write_text(gpu_source, encoding='utf8')
    objects = []
    build_log = []
    for name, path in [('protocol', ROOT/'src/protocol.cpp'), ('resources', instrumented), ('gpu', gpu_instrumented), ('probe', ROOT/'tests/resource_prefetch_native.cpp')]:
        obj = folder/(name+'.o')
        result = subprocess.run([str(compiler), *opts, '-c', str(path), '-o', str(obj)], capture_output=True, text=True, timeout=180)
        build_log.append(result.stdout+result.stderr)
        if result.returncode:
            (folder/'build.log').write_text('\n'.join(build_log), encoding='utf8')
            raise RuntimeError(result.stderr)
        objects.append(obj)
    exe = folder/'resource_prefetch.exe'
    result = subprocess.run([str(compiler), '-municode', '-static', *map(str, objects), str(ROOT/'vendor/zlib/libz.a'), '-ld3d11', '-ldxgi', '-ld3dcompiler', '-ldxguid', '-o', str(exe)], capture_output=True, text=True, timeout=90)
    build_log.append(result.stdout+result.stderr)
    (folder/'build.log').write_text('\n'.join(build_log), encoding='utf8')
    if result.returncode:
        raise RuntimeError(result.stderr)
    outcomes = []
    modes = args.modes or ['prune-cpu', 'prune-gpu', 'ready', 'priority', 'demand-wait', 'cancel', 'cancel-demand', 'hot', 'revision', 'future-error', 'publish-atomic', 'framepub-atomic', 'revision-gate', 'active-working-set']
    for mode in modes:
        result = subprocess.run([str(exe), mode, str(packs)], capture_output=True, text=True, timeout=30)
        (folder/(mode+'.log')).write_text(result.stdout+result.stderr, encoding='utf8')
        outcomes.append({'mode': mode, 'passed': result.returncode == 0, 'stdout': result.stdout, 'stderr': result.stderr})
    (folder/'results.json').write_text(json.dumps(outcomes, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps(outcomes, ensure_ascii=False))
    return 0 if all(x['passed'] for x in outcomes) else 1

if __name__ == '__main__':
    raise SystemExit(main())
