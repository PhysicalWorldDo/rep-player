"""Final GPU snapshots from unchanged client REP inputs."""
import json
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode=True
sys.path.insert(0,r'D:\DNF115us\skill_player')
from rep_protocol import load_replay, Draw
from PIL import Image, ImageDraw
import numpy as np

root=Path(r'D:\DNF115us\rep_player')
out=root/'validation'/'bloodyrave_fix'/'final'
out.mkdir(parents=True,exist_ok=True)
inputs=Path(r'D:\115us\client\Replay\SkillReplay\Swordman')
assets=Path(r'D:\115us\client\ImagePacks2')
exe=root/'build'/'rep_gpu.exe'
only_vp1='--only-vp1' in sys.argv
summary=json.loads((out/'verification.json').read_text()) if only_vp1 else {'executable':str(exe),'snapshots':[],'variants':[]}

def render(source,scene,label):
    report=out/(label+'.json')
    raw=out/(label+'.rgba')
    command=[str(exe),'--consume',str(source),str(report),str(raw),str(assets),str(scene)]
    result=subprocess.run(command,capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stderr)
    stats=json.loads(report.read_text())
    image=Image.frombytes('RGBA',(stats['width'],stats['height']),raw.read_bytes())
    png=out/(label+'.png');image.save(png)
    return {'source':str(source),'scene':scene,'report':str(report),'image':str(png),'gpu_report':stats,'command':command}

source=inputs/'BloodyRave.rep'
replay=load_replay(source)
for scene in (() if only_vp1 else (50,65,70,80,len(replay.scenes)-1)):
    row=render(source,scene,'bloodyrave_scene'+str(scene))
    row['timestamp_ms']=replay.scenes[scene].timestamp_ms
    summary['snapshots'].append(row)

for filename in (('BloodyRave_VP1.rep',) if only_vp1 else ('BloodyRave_VP1.rep','BloodyRave_VP2.rep')):
    source=inputs/filename
    if not source.exists():
        summary['variants'].append({'source':str(source),'present':False})
        continue
    replay=load_replay(source)
    candidates=[]
    for n,scene in enumerate(replay.scenes):
        matches=[x for x in scene.operations if isinstance(x,Draw) and any(s in x.path.lower() for s in ('/bloodyrave/cunsume','/bloodyrave/vptwistfront'))]
        if matches:candidates.append((n,scene.timestamp_ms,len(matches)))
    scene=candidates[len(candidates)//2][0] if candidates else len(replay.scenes)//2
    row=render(source,scene,source.stem.lower()+'_scene'+str(scene))
    row['timestamp_ms']=replay.scenes[scene].timestamp_ms
    row['suction_scenes']=len(candidates)
    if only_vp1:summary['variants']=[x for x in summary['variants'] if x['source']!=str(source)]
    summary['variants'].append(row)

before=Image.open(root/'validation'/'bloodyrave_fix'/'trace'/'before_scene65.png').convert('RGBA')
final=Image.open(out/'bloodyrave_scene65.png').convert('RGBA')
diagnostic=Image.open(root/'validation'/'bloodyrave_fix'/'trace'/'diagnostic_scene65.png').convert('RGBA')
delta=np.abs(np.array(final,dtype=np.int16)-np.array(diagnostic,dtype=np.int16))
summary['final_vs_diagnostic']={'max_channel_error':int(delta.max()),'changed_pixels':int(np.any(delta,axis=2).sum()),'identical':bool(not delta.any()),'diagnostic_note':'Diagnostic fixture negates recorded legacy angles and uses the preserved old executable; final image uses unchanged original REP with fixed native implementation.'}

comparison=Image.new('RGBA',(1600,638),(25,25,25,255))
comparison.alpha_composite(before,(0,38));comparison.alpha_composite(final,(800,38))
draw=ImageDraw.Draw(comparison)
draw.text((12,12),'Before - scene 65 / 1116 ms / original REP',fill='white')
draw.text((812,12),'Fixed - scene 65 / 1116 ms / original REP',fill='white')
comparison.save(out/'before_after.png')
(out/'verification.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps({'snapshots':len(summary['snapshots']),'variants':[{'source':x['source'],'scene':x.get('scene'),'scenes':x.get('gpu_report',{}).get('scenes'),'exact_eof':x.get('gpu_report',{}).get('exact_eof')} for x in summary['variants']],'comparison':summary['final_vs_diagnostic']},indent=2))
