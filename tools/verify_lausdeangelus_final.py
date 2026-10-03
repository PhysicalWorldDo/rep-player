"""Verify original LausDeAngelus and isolated weapon shader fixtures on final GPU build."""
import json
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode=True
sys.path.insert(0,r'D:\DNF115us\skill_player')
from rep_protocol import load_replay
from PIL import Image,ImageDraw
import numpy as np

root=Path(r'D:\DNF115us\rep_player')
folder=root/'validation'/'lausdeangelus_fix'
out=folder/'final'
out.mkdir(parents=True,exist_ok=True)
source=Path(r'D:\115us\client\Replay\SkillReplay\ATPriest\LausDeAngelus.rep')
exe=root/'build'/'rep_gpu.exe'
replay=load_replay(source)

def render(source,ordinal,label):
    report=out/(label+'.json');raw=out/(label+'.rgba');png=out/(label+'.png')
    command=[str(exe),'--consume',str(source),str(report),str(raw),r'D:\115us\client\ImagePacks2',str(ordinal)]
    result=subprocess.run(command,capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stderr)
    stats=json.loads(report.read_text())
    Image.frombytes('RGBA',(stats['width'],stats['height']),raw.read_bytes()).save(png)
    return {'source':str(source),'scene':ordinal,'report':str(report),'rgba':str(raw),'png':str(png),'stats':stats,'command':command}

def compare(label,left,right):
    a=np.frombuffer(Path(left['rgba']).read_bytes(),dtype=np.uint8).astype(np.int16)
    b=np.frombuffer(Path(right['rgba']).read_bytes(),dtype=np.uint8).astype(np.int16)
    if a.shape!=b.shape:raise ValueError((label,a.shape,b.shape))
    delta=np.abs(a-b).reshape(-1,4)
    return {'label':label,'left':left['rgba'],'right':right['rgba'],'max_channel_error':int(delta.max()),'changed_pixels':int(np.any(delta!=0,axis=1).sum()),'identical':bool(not delta.any())}

summary={'executable':str(exe),'original_input':str(source),'snapshots':[],'comparisons':[]}
for n in (0,10,20,30,40,150,400,700,1000,len(replay.scenes)-1):
    row=render(source,n,'lausdeangelus_scene'+str(n));row['timestamp_ms']=replay.scenes[n].timestamp_ms
    summary['snapshots'].append(row)
weapon_effect=render(folder/'trace'/'weapon_effect16.rep',0,'weapon_effect16')
weapon_plain=render(folder/'trace'/'weapon_plain.rep',0,'weapon_plain')
diagnostic=render(folder/'trace'/'diagnostic_weapon_null_shader.rep',0,'diagnostic_weapon_null_shader_scene0')
summary['isolated']=[weapon_effect,weapon_plain,diagnostic]
summary['comparisons'].append(compare('Effect16 native default selector equals plain draw',weapon_effect,weapon_plain))
summary['comparisons'].append(compare('Original fullscene equals isolated null shader diagnostic',summary['snapshots'][0],diagnostic))

before=Image.open(folder/'trace'/'before_scene20.png').convert('RGBA')
after=Image.open(out/'lausdeangelus_scene20.png').convert('RGBA')
w,h=before.size
comparison=Image.new('RGBA',(w*2,h+34),(25,25,25,255))
comparison.alpha_composite(before,(0,34));comparison.alpha_composite(after,(w,34))
painter=ImageDraw.Draw(comparison)
painter.text((12,10),'Before - original REP - scene 20 / 211 ms',fill='white')
painter.text((w+12,10),'Fixed - original REP - scene 20 / 211 ms',fill='white')
comparison.save(out/'before_after_scene20.png')

area=(310,335,490,465)
before_detail=before.crop(area).resize((720,520),Image.Resampling.NEAREST)
after_detail=after.crop(area).resize((720,520),Image.Resampling.NEAREST)
detail=Image.new('RGBA',(1440,554),(25,25,25,255))
detail.alpha_composite(before_detail,(0,34));detail.alpha_composite(after_detail,(720,34))
painter=ImageDraw.Draw(detail)
painter.text((12,10),'Before - weapon and hand - 4x nearest',fill='white')
painter.text((732,10),'Fixed - weapon and hand - 4x nearest',fill='white')
detail.save(out/'weapon_hand_before_after_4x.png')
after_detail.save(out/'weapon_hand_fixed_4x.png')
(out/'verification.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps({'snapshots':[(x['scene'],x['timestamp_ms'],x['stats']['scenes'],x['stats']['exact_eof'],x['stats']['fallbacks']) for x in summary['snapshots']],'comparisons':summary['comparisons']},indent=2))
if not all(x['identical'] for x in summary['comparisons']):raise SystemExit('Pixel comparison failed')
