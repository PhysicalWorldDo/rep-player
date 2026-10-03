"""Read-only BloodyRave draw trace; outputs stay in rep_player validation."""
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, r'D:\DNF115us\skill_player')
from rep_protocol import load_replay, Draw, StateOperation
from npk_reader import NpkAssets
from PIL import Image, ImageDraw
import math

root = Path(r'D:\DNF115us\rep_player')
out = root / 'validation' / 'bloodyrave_fix' / 'trace'
out.mkdir(parents=True, exist_ok=True)
replay = load_replay(r'D:\115us\client\Replay\SkillReplay\Swordman\BloodyRave.rep')
resources = dict(sorted(replay.resources.items()))
(out / 'resources.json').write_text(json.dumps(resources, indent=2), encoding='utf-8')
summary = {'version': replay.version, 'minor': replay.header.minor, 'width':replay.width, 'height':replay.height, 'render_mode':replay.header.render_mode, 'scenes':len(replay.scenes), 'resources':len(resources)}
print(json.dumps(summary))
print(json.dumps({k:v for k,v in resources.items() if any(s in v.lower() for s in ('bloody','rave','twist','suck','blood'))}, indent=2))
rows = []
for index, scene in enumerate(replay.scenes):
    for ordinal, draw in enumerate(scene.operations):
        if isinstance(draw, Draw) and any(s in draw.path.lower() for s in ('bloody','rave','twist','suck','blood')):
            row = {'scene':index, 'ms':scene.timestamp_ms, 'operation':ordinal, 'opcode':draw.opcode, 'path':draw.path, 'resource':draw.resource_id, 'frame':draw.frame, 'xy':[draw.x, draw.y], 'layer':draw.layer, 'fields':draw.fields, 'native_fields':draw.native_fields, 'params':draw.params.hex()}
            rows.append(row)
(out / 'suction_draws.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
print('Matching draws',len(rows))
for index in (0, len(replay.scenes)//3, len(replay.scenes)//2, len(replay.scenes)*2//3):
    scene = replay.scenes[index]
    print('Scene',index,scene.timestamp_ms)
    print(json.dumps([{'op':x.opcode, 'path':x.path, 'frame':x.frame, 'xy':[x.x,x.y], 'native':x.native_fields} for x in scene.operations if isinstance(x,Draw) and x.resource_id in {r['resource'] for r in rows}], indent=2))

assets = NpkAssets(r'D:\115us\client\ImagePacks2')
target_ids = (35,44,54)
selected = [x for x in rows if x['resource'] in target_ids]
metadata = []
montage = Image.new('RGBA',(1200,1000),(45,45,45,255))
painter = ImageDraw.Draw(montage)
for n,(resource,index) in enumerate(((35,0),(35,2),(35,5),(35,12),(44,0),(44,2),(44,5),(44,12),(54,2),(54,4),(54,6),(54,10))):
    path = resources[resource]
    frame = assets.frame(path,index)
    rgba = frame.rgba.copy()
    rgba.thumbnail((280,215))
    x=(n%4)*300;y=(n//4)*330
    montage.alpha_composite(rgba,(x,y+35))
    painter.text((x,y),f'{Path(path).name} frame{index}',fill='white')
    painter.text((x,y+265),f'offset {frame.offset_x},{frame.offset_y}',fill='white')
    painter.text((x,y+280),f'crop {frame.width}x{frame.height} full {frame.full_width}x{frame.full_height}',fill='white')
    metadata.append({'path':path,'resource':resource,'frame':index,'offset':[frame.offset_x,frame.offset_y],'crop':[frame.width,frame.height],'full':[frame.full_width,frame.full_height],'texture_rect':frame.texture_rect,'texture_rotation':frame.texture_rotation,'entry':assets.entries[path]})
montage.save(out/'resource_montage.png')
(out/'resource_metadata.json').write_text(json.dumps(metadata,indent=2),encoding='utf-8')
for scene_index in (30,40,50,60,70,75):
    trace = [x for x in selected if x['scene']==scene_index]
    print('Selected',scene_index,json.dumps([{'path':x['path'],'frame':x['frame'],'xy':x['xy'],'rotation':x['fields']['rotation'],'scale':x['fields']['scale'],'pivot':x['fields']['pivot']} for x in trace]))
