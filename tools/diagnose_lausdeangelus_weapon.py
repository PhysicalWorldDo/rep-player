"""Isolate the original first weapon draw with and without its live shader."""
import json
from pathlib import Path
import struct
import subprocess
import sys
sys.dont_write_bytecode=True
sys.path.insert(0,r'D:\DNF115us\skill_player')
from rep_protocol import load_replay,Draw,_read_container,_dictionary,_decode_commands,_Reader
from test_rep_protocol_versions import pack_replay
from PIL import Image
import numpy as np

root=Path(r'D:\DNF115us\rep_player')
out=root/'validation'/'lausdeangelus_fix'/'trace'
source=Path(r'D:\115us\client\Replay\SkillReplay\ATPriest\LausDeAngelus.rep')
replay=load_replay(source)
weapon=next(x for x in replay.scenes[0].operations if isinstance(x,Draw) and x.resource_id==27)
params=bytearray(weapon.params);struct.pack_into('<I',params,0,0)
draw=struct.pack('<II',3,64)+params
aux=struct.pack('<hh',weapon.x,weapon.y)
effect=weapon.filters[-1]
shader=struct.pack('<I',19)+struct.pack('<BIIBII',1,effect.type,len(effect.floats),0,0,len(effect.int_bits))+struct.pack('<'+'f'*len(effect.floats),*effect.floats)+struct.pack('<'+'I'*len(effect.int_bits),*effect.int_bits)
summary=[]
for label,raw in (('weapon_effect16',shader+draw+struct.pack('<I',20)),('weapon_plain',draw)):
    rep=out/(label+'.rep');rep.write_bytes(pack_replay(replay.version,{0:raw},[(0,(0,),aux)],[weapon.path],replay.header.raw))
    report=rep.with_suffix('.json');rgba=rep.with_suffix('.rgba')
    result=subprocess.run([str(root/'build'/'rep_gpu_lausdeangelus_before.exe'),'--consume',str(rep),str(report),str(rgba)],capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stderr)
    stats=json.loads(report.read_text())
    image=Image.frombytes('RGBA',(stats['width'],stats['height']),rgba.read_bytes());image.save(rep.with_suffix('.png'))
    a=np.array(image)[:,:,:3];ys,xs=np.where(np.any(a!=0,axis=2))
    summary.append({'fixture':str(rep),'image':str(rep.with_suffix('.png')),'nonblack_bounds':[int(xs.min()),int(ys.min()),int(xs.max()+1),int(ys.max()+1)],'pixels':len(xs)})

version,chunks,header=_read_container(source)
commands,resources=_dictionary(chunks[1],version)
exact=[]
for key in replay.scenes[0].command_ids:
    for op,payload in _decode_commands(commands[key],key,version,header.minor):
        if op in (3,4) and payload['fields']['resource_id']==27:
            exact.append({'command_id':key,'opcode':op,'raw':commands[key].hex(),'fields':payload['fields'],'xy':[weapon.x,weapon.y],'stored_size':payload['stored_size'],'live_shader':{'type':effect.type,'floats':effect.floats,'bits':effect.int_bits}})
(out/'scene0_exact_weapon_commands.json').write_text(json.dumps(exact,indent=2),encoding='utf-8')
(out/'isolated_shader_diagnostics.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps({'exact':exact,'isolated':summary},indent=2))

changed={}
changed_keys=[]
for key,raw in commands.items():
    decoded=_decode_commands(raw,key,version,header.minor)
    has_weapon=any(op in (3,4) and payload['fields']['resource_id']==27 for op,payload in decoded)
    replacement=raw
    if has_weapon:
        marker=struct.pack('<IBII',19,1,16,1)
        offset=raw.find(marker)
        if offset>=0:
            data=bytearray(raw);struct.pack_into('<I',data,offset+5,22);replacement=bytes(data)
            changed_keys.append(key)
    changed[key]=replacement
timeline=[];reader=_Reader(chunks[0],'timeline')
while reader.pos<len(reader.data):
    ms,size=reader.unpack('<iI');ids=reader.unpack('<'+'I'*(size//4));aux=reader.take(reader.u32());timeline.append((ms,ids,aux))
changed=dict(sorted(changed.items(),key=lambda item:len(item[1])))
rep=out/'diagnostic_weapon_null_shader.rep'
rep.write_bytes(pack_replay(version,changed,timeline,[resources[i] for i in range(len(resources))],header.raw))
report=rep.with_suffix('.json');rgba=rep.with_suffix('.rgba')
result=subprocess.run([str(root/'build'/'rep_gpu_lausdeangelus_before.exe'),'--consume',str(rep),str(report),str(rgba),r'D:\115us\client\ImagePacks2','0'],capture_output=True,text=True)
if result.returncode:raise RuntimeError(result.stderr)
stats=json.loads(report.read_text());Image.frombytes('RGBA',(stats['width'],stats['height']),rgba.read_bytes()).save(rep.with_suffix('.png'))
(out/'diagnostic_weapon_null_shader_changes.json').write_text(json.dumps({'changed_command_ids':changed_keys,'source':str(source),'source_unchanged':True,'note':'Only local diagnostic dictionary Effect16 pushes associated with resource27 become native null-program22; production source and client input are unchanged.'},indent=2),encoding='utf-8')
print('Fullscene diagnostic',rep,'changed',len(changed_keys))
