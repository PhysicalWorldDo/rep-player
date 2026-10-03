"""Generate a local-only diagnostic REP with the sine angle sign reversed."""
import json
import math
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode=True
sys.path.insert(0,r'D:\DNF115us\skill_player')
from rep_protocol import _read_container,_dictionary,_decode_commands,_Reader,load_replay
from test_rep_protocol_versions import pack_replay
from npk_reader import NpkAssets

source=Path(r'D:\115us\client\Replay\SkillReplay\Swordman\BloodyRave.rep')
out=Path(r'D:\DNF115us\rep_player\validation\bloodyrave_fix\trace')
version,chunks,header=_read_container(source)
commands,resources=_dictionary(chunks[1],version)
changed={}
trace=[]
for key,raw in commands.items():
    data=bytearray(raw)
    seek=0
    for op,payload in _decode_commands(raw,key,version,header.minor):
        if op in (3,4):
            size=payload['stored_size']
            needle=struct.pack('<II',op,size)+payload['params'][:size]
            offset=raw.find(needle,seek)
            if offset<0:raise ValueError((key,op))
            seek=offset+len(needle)
            angle=struct.unpack_from('<f',data,offset+8+16)[0] if size>=20 else 0
            if angle:
                struct.pack_into('<f',data,offset+8+16,-angle)
                trace.append({'command':key,'op':op,'size':size,'raw_offset':offset,'params':payload['fields']})
    changed[key]=bytes(data)
reader=_Reader(chunks[0],'timeline')
scenes=[]
while reader.pos<len(reader.data):
    ms,size=reader.unpack('<iI')
    ids=reader.unpack('<'+'I'*(size//4))
    aux=reader.take(reader.u32())
    scenes.append((ms,ids,aux))
changed=dict(sorted(changed.items(),key=lambda item:len(item[1])))
target=out/'diagnostic_reverse_angles.rep'
target.write_bytes(pack_replay(version,changed,scenes,[resources[i] for i in range(len(resources))],header.raw))
(out/'diagnostic_reverse_angles_commands.json').write_text(json.dumps(trace,indent=2),encoding='utf-8')
print('Changed angle commands',len(trace))

replay=load_replay(source)
assets=NpkAssets(r'D:\115us\client\ImagePacks2')
evidence=[]
for draw in replay.scenes[65].operations:
    if getattr(draw,'resource_id',None) not in (35,44) or not hasattr(draw,'frame'):continue
    f=assets.frame(draw.path,draw.frame)
    fields=draw.fields
    sx,sy=fields['scale'];px,py=fields['pivot'];direction=-1 if fields['flags_6']==1 else 1
    corners={}
    for sign in (1,-1):
        angle=fields['rotation']*sign;c=math.cos(angle);sn=math.sin(angle)
        a=direction*c*sx;b=-direction*sn*sy;cc=sn*sx;d=c*sy
        x=draw.x+px+a*(f.offset_x-px)+b*(f.offset_y-py)
        y=draw.y+py+cc*(f.offset_x-px)+d*(f.offset_y-py)
        points=[(a*xx+b*yy+x,cc*xx+d*yy+y) for xx,yy in ((0,0),(f.width,0),(0,f.height),(f.width,f.height))]
        corners['current' if sign==1 else 'reverse_sine']={'points':points,'bounds':[min(p[0] for p in points),min(p[1] for p in points),max(p[0] for p in points),max(p[1] for p in points)]}
    evidence.append({'path':draw.path,'frame':draw.frame,'xy':[draw.x,draw.y],'fields':fields,'metadata':{'offset':[f.offset_x,f.offset_y],'crop':[f.width,f.height],'full':[f.full_width,f.full_height]},'corners':corners})
(out/'scene65_rotation_position.json').write_text(json.dumps(evidence,indent=2),encoding='utf-8')
print(json.dumps(evidence,indent=2))
