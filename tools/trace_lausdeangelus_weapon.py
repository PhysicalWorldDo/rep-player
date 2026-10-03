"""Read-only LausDeAngelus weapon/resource and GPU scene tracing."""
import json
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode=True
sys.path.insert(0,r'D:\DNF115us\skill_player')
from rep_protocol import load_replay,Draw,StateOperation
from npk_reader import NpkAssets
from PIL import Image

root=Path(r'D:\DNF115us\rep_player')
out=root/'validation'/'lausdeangelus_fix'/'trace'
out.mkdir(parents=True,exist_ok=True)
source=Path(r'D:\115us\client\Replay\SkillReplay\ATPriest\LausDeAngelus.rep')
replay=load_replay(source)
resources=dict(sorted(replay.resources.items()))
(out/'resources.json').write_text(json.dumps(resources,indent=2),encoding='utf-8')
summary={'source':str(source),'version':replay.version,'minor':replay.header.minor,'width':replay.width,'height':replay.height,'render_mode':replay.header.render_mode,'scenes':len(replay.scenes),'resources':len(resources)}
print(json.dumps(summary))
weapon_ids={k for k,v in resources.items() if any(s in v.lower() for s in ('weapon','rosary','cross','totem','bead','scythe'))}
print('Weapons',json.dumps({k:resources[k] for k in weapon_ids},indent=2))
rows=[]
assets=NpkAssets(r'D:\115us\client\ImagePacks2')
meta={}
for n,scene in enumerate(replay.scenes):
    for ordinal,draw in enumerate(scene.operations):
        if isinstance(draw,Draw) and draw.resource_id in weapon_ids:
            row={'scene':n,'ms':scene.timestamp_ms,'operation':ordinal,'opcode':draw.opcode,'resource':draw.resource_id,'path':draw.path,'frame':draw.frame,'xy':[draw.x,draw.y],'layer':draw.layer,'fields':draw.fields,'native_fields':draw.native_fields,'params':draw.params.hex()}
            rows.append(row)
            key=(draw.path,draw.frame)
            if key not in meta:
                f=assets.frame(draw.path,draw.frame)
                meta[key]={'path':draw.path,'frame':draw.frame,'offset':[f.offset_x,f.offset_y],'crop':[f.width,f.height],'full':[f.full_width,f.full_height],'texture_rotation':f.texture_rotation,'texture_rect':f.texture_rect,'entry':assets.entries[draw.path]}
                if len(meta)<=12:f.rgba.save(out/('weapon_'+str(draw.resource_id)+'_frame'+str(draw.frame)+'.png'))
(out/'weapon_draws.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
(out/'weapon_metadata.json').write_text(json.dumps(list(meta.values()),indent=2),encoding='utf-8')
print('Weapon draws',len(rows),'uniqueframes',len(meta))
print(json.dumps(rows[:12],indent=2))
snapshots=[]
for n in (0,10,20,30,40):
    report=out/('before_scene'+str(n)+'.json');rgba=out/('before_scene'+str(n)+'.rgba')
    result=subprocess.run([str(root/'build'/'rep_gpu.exe'),'--consume',str(source),str(report),str(rgba),r'D:\115us\client\ImagePacks2',str(n)],capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stderr)
    stats=json.loads(report.read_text())
    Image.frombytes('RGBA',(stats['width'],stats['height']),rgba.read_bytes()).save(rgba.with_suffix('.png'))
    snapshots.append({'scene':n,'ms':replay.scenes[n].timestamp_ms,'stats':stats})
summary['snapshots']=snapshots
(out/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
