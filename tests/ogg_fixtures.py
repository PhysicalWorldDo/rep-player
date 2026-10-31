"""Small generated REP/NPK/OGG inputs; no client or external project is used."""
from pathlib import Path
import math
import struct
import subprocess
import wave
import zlib


OGG_A = 'sounds/effect/fixture/tone_a.ogg'
OGG_B = 'sounds/effect/fixture/tone_b.ogg'
IMAGE = 'sprite/test/frame.img'


def package(entries):
    key = (b'puchikon@neople dungeon and fighter ' + b'DNF' * 100)[:255] + b'\0'
    offset = 20 + len(entries) * 264
    table, payloads = bytearray(), bytearray()
    for logical, payload in entries:
        name = logical.encode('ascii').ljust(256, b'\0')
        table += struct.pack('<II', offset, len(payload))
        table += bytes(a ^ b for a, b in zip(name, key))
        payloads += payload
        offset += len(payload)
    return b'NeoplePack_Bill\0' + struct.pack('<I', len(entries)) + table + payloads


def pack_replay(commands, scenes, resources):
    dictionary = struct.pack('<I', len(commands))
    previous, largest = b'', -1
    for key, raw in commands.items():
        encoded = raw if len(raw) > largest else bytes(a ^ b for a, b in zip(raw, previous))
        dictionary += struct.pack('<IH', key, len(raw)) + encoded
        previous, largest = raw, max(largest, len(raw))
    dictionary += struct.pack('<I', len(resources))
    for text in resources:
        value = text.encode('ascii')
        dictionary += b'\1' + struct.pack('<H', len(value)) + value + b'\0'
    timeline = bytearray()
    for timestamp, ids in scenes:
        # Only op3 uses four auxiliary bytes; op6 does not consume auxiliary data.
        draw_count = sum(struct.unpack_from('<I', commands[key])[0] == 3 for key in ids)
        aux = struct.pack('<2h', 2, 3) * draw_count
        timeline += struct.pack('<iI', timestamp, len(ids) * 4)
        timeline += struct.pack('<' + 'I' * len(ids), *ids) + struct.pack('<I', len(aux)) + aux
    header = b'\x0b\0' + struct.pack('<8h', *([16, 16] * 4))
    body = b'\1' + struct.pack('<f', 1.7) + b'\0' + struct.pack('<I', len(header)) + header
    for block in (timeline, dictionary):
        compressed = zlib.compress(block)
        body += struct.pack('<I', len(compressed)) + compressed
    return struct.pack('<II', zlib.crc32(body), len(body)) + body


def sound(resource, owner):
    return struct.pack('<II9i', 6, 36, resource, 0, 0, owner, 1, 0, 0, -1, -1)


def make_fixture(root: Path, ffmpeg: Path):
    """Create two 300 ms OGG files and a five-second replay in owned root.

    The first two records share timestamp zero, but call A and B separately.
    A loops; A_ALIAS shares its file. B is a single shot, requested again later.
    """
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    client = root / 'client'
    for directory in ('SoundPacks', 'ImagePacks2', 'Replay'):
        (client / directory).mkdir(parents=True, exist_ok=True)
    payloads = []
    for name, frequency in (('tone_a', 440), ('tone_b', 880)):
        wav, ogg = root / (name + '.wav'), root / (name + '.ogg')
        with wave.open(str(wav), 'wb') as output:
            output.setparams((1, 2, 48000, 0, 'NONE', 'not compressed'))
            output.writeframes(b''.join(struct.pack('<h', round(6000 * math.sin(n * math.tau * frequency / 48000)))
                                        for n in range(14400)))
        result = subprocess.run([str(ffmpeg), '-v', 'error', '-y', '-i', str(wav),
                                 '-c:a', 'libvorbis', str(ogg)], capture_output=True, timeout=30)
        if result.returncode:
            raise RuntimeError(result.stderr.decode('utf8', errors='replace'))
        payloads.append(ogg.read_bytes())
    (client / 'SoundPacks' / 'sounds_effect_fixture.npk').write_bytes(package([
        (OGG_A, payloads[0]), (OGG_B, payloads[1])]))
    raw = bytes((0, 80, 220, 255)) * 16
    image = b'Neople Img File\0' + struct.pack('<4I', 36, 0, 2, 1)
    image += struct.pack('<9i', 16, 5, 4, 4, len(raw), 0, 0, 4, 4) + raw
    (client / 'ImagePacks2' / 'sprite_test.NPK').write_bytes(package([(IMAGE, image)]))
    (client / 'audio.xml').write_text(
        '<AUDIO><EFFECT ID="TONE_A" FILE="' + OGG_A + '" LOOP_DELAY="0"/>'
        '<VOICE ID="TONE_A_ALIAS" FILE="' + OGG_A + '"/>'
        '<EFFECT ID="TONE_B" FILE="' + OGG_B + '"/></AUDIO>', encoding='utf8')
    params = bytearray(64)
    struct.pack_into('<I', params, 0, 3)
    struct.pack_into('<I', params, 8, 0xffffffff)
    struct.pack_into('<2f', params, 20, 1, 1)
    struct.pack_into('<2f', params, 28, 0, 0)
    struct.pack_into('<2f', params, 52, 1, 1)
    commands = {0: sound(0, 11), 1: sound(1, 12), 2: sound(2, 13),
                3: struct.pack('<II', 3, 64) + params}
    scenes = [(0, (0, 3)), (0, (2, 3))]
    for timestamp in range(100, 5001, 100):
        ids = (2, 3) if timestamp in (500, 1500, 2500, 3500, 4500) else (1, 3) if timestamp == 1000 else (3,)
        scenes.append((timestamp, ids))
    replay = client / 'Replay' / 'ogg-controls.rep'
    replay.write_bytes(pack_replay(commands, scenes, ['TONE_A', 'TONE_A_ALIAS', 'TONE_B', IMAGE]))
    other = client / 'Replay' / 'other.rep'
    other.write_bytes(pack_replay(commands, [(0, (2, 3)), (2000, (3,))],
                                 ['TONE_A', 'TONE_A_ALIAS', 'TONE_B', IMAGE]))
    return {'client': client, 'replay': replay, 'otherReplay': other,
            'oggA': root / 'tone_a.ogg', 'oggB': root / 'tone_b.ogg'}
