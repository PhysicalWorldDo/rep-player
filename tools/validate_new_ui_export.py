"""Decode actual BloodyRave exports; all generated evidence stays in rep_player."""
import json
from pathlib import Path
import subprocess
import time
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
CLIENT = Path(r'D:\115us\client')
EXE = ROOT / 'build' / 'rep_export.exe'
FFMPEG = ROOT / 'build' / 'ffmpeg.exe'
FFPROBE = Path(r'D:\ffmpeg\ffprobe.exe')
FOLDER = ROOT / 'validation' / 'new_ui_export' / ('bloodyrave_' + time.strftime('%Y%m%d_%H%M%S'))
HIDDEN = ['sprite/map/chn_pvp/chn_studio/map1_tile.img',
          'sprite/map/chn_pvp/chn_studio/map1_tile_ex.img']


def checked(command, **kwargs):
    result = subprocess.run(list(map(str, command)), capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(str(command) + '\n' + str(result.stderr))
    return result


def alpha_stats(pixels):
    alpha = pixels[..., 3]
    return {'transparent_pixels': int(np.count_nonzero(alpha == 0)),
            'visible_pixels': int(np.count_nonzero(alpha > 0)),
            'partial_pixels': int(np.count_nonzero((alpha > 0) & (alpha < 255))),
            'alpha_min': int(alpha.min()), 'alpha_max': int(alpha.max()),
            'nonzero_rgb_pixels': int(np.count_nonzero(np.any(pixels[..., :3] > 0, axis=-1)))}


def main():
    FOLDER.mkdir(parents=True)
    result = {'replay': str(CLIENT / 'Replay' / 'SkillReplay' / 'Swordman' / 'BloodyRave.rep'),
              'hidden_images': HIDDEN, 'exports': []}
    for format, fps in [('png', 60), ('mov', 60), ('mp4', 30)]:
        command = [EXE, '--client', CLIENT, '--replay', result['replay'], '--format', format,
                   '--fps', fps, '--alpha', '1', '--output', FOLDER, '--name', 'BloodyRave_' + format]
        for path in HIDDEN:
            command += ['--hide', path]
        start = time.perf_counter()
        exported = json.loads(checked(command, text=True, encoding='utf-8').stdout)
        exported['wall_seconds'] = time.perf_counter() - start
        output = Path(exported['output'])
        assert (exported['width'], exported['height'], exported['duration_ms'], exported['executed_scenes']) == (800, 600, 3282, 197)
        assert exported['frames'] == (3282 * fps + 999) // 1000 + 1
        if format == 'png':
            files = sorted(output.glob('frame_*.png'))
            assert len(files) == exported['frames']
            pixels = np.asarray(Image.open(files[67]).convert('RGBA'))
            assert pixels.shape == (600, 800, 4)
            Image.fromarray(pixels).save(FOLDER / 'png_scene65.png')
        else:
            probe = json.loads(checked([FFPROBE, '-v', 'error', '-show_streams', '-show_format', '-of', 'json', output], text=True).stdout)
            streams = probe['streams']
            assert len(streams) == 1, 'Export must contain one video stream and no audio'
            stream = streams[0]
            assert (stream['width'], stream['height'], stream['r_frame_rate'], int(stream['nb_frames'])) == (800, 600, str(fps) + '/1', exported['frames'])
            assert abs(float(stream['duration']) - exported['frames'] / fps) < 0.002
            assert float(stream['duration']) >= 3.282
            assert float(stream['duration']) < 3.282 + 2 / fps
            if format == 'mov':
                assert (stream['codec_name'], stream['profile']) == ('prores', '4444')
                assert stream['pix_fmt'].startswith('yuva')
            else:
                assert stream['codec_name'] == 'h264'
                assert not exported['alpha']
            # A complete decode checks every frame, then sample the scene65 region.
            checked([FFMPEG, '-v', 'error', '-i', output, '-an', '-f', 'null', '-'])
            sample = 67 if fps == 60 else 34
            decoded = checked([FFMPEG, '-v', 'error', '-i', output, '-vf', 'select=eq(n\\,' + str(sample) + ')',
                               '-frames:v', '1', '-f', 'rawvideo', '-pix_fmt', 'rgba', 'pipe:1']).stdout
            pixels = np.frombuffer(decoded, np.uint8).reshape(600, 800, 4)
            Image.fromarray(pixels).save(FOLDER / (format + '_sample.png'))
            exported['stream'] = stream
        exported['alpha_stats'] = alpha_stats(pixels)
        stats = exported['alpha_stats']
        assert stats['visible_pixels'] > 1000
        assert stats['nonzero_rgb_pixels'] > 1000
        if format in ('mov', 'png'):
            assert stats['transparent_pixels'] > 1000
        else:
            assert (stats['alpha_min'], stats['alpha_max']) == (255, 255)
        result['exports'].append(exported)
        (FOLDER / 'summary.json').write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
        print(format, fps, exported['frames'], stats, flush=True)
    # Transparent material over black must retain the renderer's visible energy,
    # including additive draws whose premultiplied RGB can exceed native alpha.
    command = [EXE, '--client', CLIENT, '--replay', result['replay'], '--format', 'png',
               '--fps', '60', '--alpha', '0', '--output', FOLDER, '--name', 'BloodyRave_opaque_reference']
    for path in HIDDEN:
        command += ['--hide', path]
    opaque = json.loads(checked(command, text=True, encoding='utf-8').stdout)
    alpha_pixels = np.asarray(Image.open(Path(result['exports'][0]['output']) / 'frame_000067.png').convert('RGBA'))
    opaque_pixels = np.asarray(Image.open(Path(opaque['output']) / 'frame_000067.png').convert('RGBA'))
    composite = np.rint(alpha_pixels[..., :3].astype(float) * alpha_pixels[..., 3:4] / 255).astype(np.uint8)
    difference = np.abs(composite.astype(int) - opaque_pixels[..., :3].astype(int))
    result['alpha_black_composite'] = {
        'sample_frame': 67, 'max_rgb_difference': int(difference.max()),
        'pixels_difference_gt_2': int(np.count_nonzero(np.any(difference > 2, axis=-1))),
        'mean_rgb_difference': float(difference.mean()), 'opaque_output': opaque['output']}
    Image.fromarray(composite).save(FOLDER / 'alpha_over_black.png')
    Image.fromarray(opaque_pixels[..., :3]).save(FOLDER / 'opaque_same_sample.png')
    (FOLDER / 'summary.json').write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
    assert result['alpha_black_composite']['max_rgb_difference'] <= 2, result['alpha_black_composite']
    print('alpha over black', result['alpha_black_composite'], flush=True)
    result['passed'] = True
    (FOLDER / 'summary.json').write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
    print(FOLDER / 'summary.json', flush=True)


if __name__ == '__main__':
    main()
