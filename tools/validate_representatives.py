import json
from pathlib import Path
import subprocess
import time
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(r'D:\115us\client\Replay\SkillReplay')
records=json.loads(Path(r'D:\DNF115us\skill_player\representative_replays.json').read_text(encoding='utf8'))['records']
out=ROOT/'validation'/'representatives';out.mkdir(exist_ok=True)
results=[];start=time.perf_counter()
for index,record in enumerate(records):
    relative=Path(record['path']);name=f'{index:02d}_{relative.stem}'
    report=out/(name+'.json');rgba=out/(name+'.rgba');before=time.perf_counter()
    run=subprocess.run([str(ROOT/'build'/'rep_gpu.exe'),'--consume',str(SOURCE/relative),str(report),str(rgba)],capture_output=True,text=True)
    row={'path':record['path'],'process_seconds':time.perf_counter()-before,'pass':False}
    if run.returncode:row['error']=run.stderr.strip()
    else:
        data=json.loads(report.read_text());row.update(data);row['pass']=data['exact_eof'] and data['scenes']==record['frames']
        Image.frombytes('RGBA',(data['width'],data['height']),rgba.read_bytes()).save(out/(name+'.png'))
    results.append(row);print(json.dumps(row),flush=True)
summary={'files':len(results),'passed':sum(x['pass'] for x in results),'scenes':sum(x.get('scenes',0) for x in results),'seconds':time.perf_counter()-start,'records':results}
(out/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf8')
print(json.dumps({k:v for k,v in summary.items() if k!='records'}))
raise SystemExit(any(not x['pass'] for x in results))
