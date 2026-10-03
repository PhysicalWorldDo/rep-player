"""GPU-versus-original-Python shader oracle. Oracle/JIT files are isolated."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
OLD = Path(r'D:\DNF115us\skill_player')
COPY = ROOT / 'validation' / 'oracle'
COPY.mkdir(exist_ok=True)
for path in OLD.glob('*.py'):
    shutil.copyfile(path, COPY / path.name)
for pattern in ('shader*.json', 'rep_resource_migration_rules.json'):
    for path in OLD.glob(pattern):
        shutil.copyfile(path, COPY / path.name)
sys.dont_write_bytecode = True
sys.path.insert(0, str(COPY))
os.environ['NUMBA_CACHE_DIR'] = str(ROOT / 'validation' / 'numba_cache')
import numpy as np
from PIL import Image
import shader_complex
import inspect
import re
# Adapt only the isolated oracle: D3D MAD rounds once, and this GPU's
# bilinear sampler uses eight fractional bits for interpolation weights.
vm = inspect.getsource(shader_complex._execute_pixel_tile)
vm = vm.replace("elif op == 'mad': value = values[0] * values[1] + values[2]",
                "elif op == 'mad': value = (values[0].astype(np.float64) * values[1].astype(np.float64) + values[2].astype(np.float64)).astype(np.float32)")
exec(compile(vm, 'isolated_fused_mad', 'exec'), shader_complex.__dict__)
sampler = inspect.getsource(shader_complex.sample_texture)
sampler = sampler.replace("fx, fy = (x - x0)[..., None], (y - y0)[..., None]",
                          "fx, fy = (np.rint((x-x0)*256)/256)[..., None], (np.rint((y-y0)*256)/256)[..., None]")
exec(compile(sampler, 'isolated_sampler_weights', 'exec'), shader_complex.__dict__)
original_operand = shader_complex._operand
def exact_integer_literals(token):
    result = original_operand(token)
    if result[0] == 'literal':
        text = token.strip().lstrip('-').strip('|')
        values = text[2:-1].split(',')
        bits = [int(v.strip()) & 0xffffffff if re.fullmatch(r'-?\d+', v.strip()) else int(x)
                for v,x in zip(values,result[1].view(np.uint32)[:len(values)])]
        if len(bits)==1:bits*=4
        return result[0],np.asarray(bits,np.uint32).view(np.float32),result[2],result[3],result[4]
    return result
shader_complex._operand = exact_integer_literals
native_program = None
native_type = None
bound_frames=[]
registry = {r['program_id']:r for r in json.loads((ROOT/'assets'/'shaders'/'manifest.json').read_text())}
original_inputs=shader_complex._inputs
def native_inputs(width,height,*args,**kwargs):
    # StaticBranch's old CPU renderer inferred a 3x expanded quad. Native
    # B3250/B38F0/B36E0 and 147078AF0 retain the original image geometry.
    if native_type==16 and any(kwargs.get('padding',(0,0))):
        px,py=kwargs['padding'];width-=2*px;height-=2*py;kwargs['padding']=(0,0)
    return original_inputs(width,height,*args,**kwargs)
shader_complex._inputs=native_inputs
def direct(assembly, inputs, constants, textures, **kwargs):
    constants={k:v.copy() for k,v in constants.items()}
    textures=dict(textures)
    # Native E55E0/E5C40 binds the full bitmap and separate atlas fields.
    slots={12:[1],13:[1],15:[1],16:[1],17:[1,2],36:[1,2],38:[1,2],39:[1],47:[1],49:[3],52:[1]}.get(native_type,[])
    if native_type==17:
        # The historical backend resolves the mask (t2) before frame 4 (t1).
        slots=([2] if int(floats[10])!=-1 else [])+[1]
    elif native_type==36:
        data=np.asarray(bits,np.uint32).view(np.float32)
        paths=context.get('resource_paths',{})
        slots=[slot for slot,index in ((1,20),(2,22))
               if len(data)==24 and paths.get(int(data[index])) and int(data[index+1])>0]
    for slot,frame in zip(slots,bound_frames):
        pixels,rect,rotated=frame
        h,w=pixels.shape[:2];scale=[(rect[2]-rect[0])/w,(rect[3]-rect[1])/h]
        if rotated:scale.reverse()
        constants[f'cb1[{3+slot}]']=np.array((rect[0]/w,rect[1]/h,*scale),np.float32)
        textures[f't{slot}']=pixels.astype(np.float32)/255
        constants[f'cb1[{slot}]']=np.array((w,h,1,1),np.float32)
    for n in range(4):
        entry=constants[f'cb1[{n}]'];entry[2:]=1-.5/entry[:2]
    pixel = inputs.slice_rows(0, inputs.height)
    # Execute the original VS before the PS. The historical oracle omitted
    # VS-generated world-coordinate varyings in MaskBlur and MaskCircleOut.
    if native_program is not None and native_program in (83,79):
        vertex = {f'v{n}':np.zeros_like(pixel['v0']) for n in range(10)}
        vertex['v0'][...,:3] = pixel['v0'][...,:3]
        vertex['v1'] = pixel['v1']
        vertex['v2'][...,:2] = pixel['v2'][...,:2]
        vertex['v3'][...,:2] = pixel['v2'][...,2:]
        vertex['v4'][...,0] = 1
        vertex['v5'][...,:2] = pixel['v3'][...,2:]
        vertex['v6'][...,:2] = pixel['v4'][...,:2]
        vertex['v7'][...,:2] = pixel['v4'][...,2:]
        vertex['v8'][...,:2] = pixel['v5'][...,:2]
        vertex['v9'][...,:2] = pixel['v5'][...,2:]
        matrices = np.eye(4,dtype=np.float32).reshape(-1).tolist()+[2/16,0,0,-1,0,-2/16,0,1,0,0,1,0,0,0,0,1]
        uniforms = {**constants,**{f'cb0[{n}]':np.asarray(matrices[n*4:n*4+4],np.float32) for n in range(8)}}
        vs = (ROOT/'assets'/'shaders'/(registry[native_program]['vs']+'.asm')).read_text()
        for n in range(1,6):
            return_register = vs.replace('ret ',f'mov o0.xyzw, o{n}.xyzw\nret ')
            pixel[f'v{n}'] = shader_complex._execute_pixel_tile(return_register, vertex, uniforms, {})
    result = shader_complex._execute_pixel_tile(assembly, pixel, constants, textures)
    return shader_complex._quantize(result) if kwargs.get('quantize') else result
shader_complex.execute_pixel_program = direct
import shader_legal_a
# NumPy promoted level * -1000 to float64 before int(), while native C++
# performs float32 multiplication. The different integer changes fade bounds.
gauge = inspect.getsource(shader_legal_a.apply_legal_a)
gauge = gauge.replace('level=float(data[6])', 'level=np.float32(data[6])')
gauge = gauge.replace('transition=np.clip((-850-int(np.float32(level)*-1000))/150,0,1)',
                      'transition=np.clip(np.float32(-850-int(level*np.float32(-1000)))/np.float32(150),np.float32(0),np.float32(1))')
gauge = gauge.replace('fade_in=float(data[7]); fade_out=float(data[8])',
                      'fade_in=np.float32(data[7]); fade_out=np.float32(data[8])')
gauge = gauge.replace('fade_out*(1-transition)', 'fade_out*(np.float32(1)-transition)')
exec(compile(gauge, 'isolated_gauge_float32', 'exec'), shader_legal_a.__dict__)
from shader_backend import apply_effect
from rep_protocol import Effect
from npk_reader import NpkAssets

assets = NpkAssets(r'D:\115us\client\ImagePacks2')
groups = ('mask', 'dissolvemask', 'flametexture', 'glitchtexture', 'waterdistortion', 'delezieeffecttexture')
def resolve(index, group=0):
    frame=assets.native_frame('sprite/shader/' + groups[group] + '.img', index)
    if native_type in (24,40):return frame
    return full_texture(frame)

def full_texture(frame):
    pixels=np.asarray(frame.texture if frame.texture is not None else frame.rgba)
    h,w=pixels.shape[:2]
    if w<=512 and h<=512 and not frame.native_empty:
        pw=max(16,1<<(w-1).bit_length());ph=max(16,1<<(h-1).bit_length())
        padded=np.zeros((ph,pw,4),np.uint8);padded[:h,:w]=pixels;pixels=padded
    rect=frame.texture_rect if frame.texture_rect is not None else (0,0,w,h)
    bound_frames.append((pixels,rect,bool(frame.texture_rotation)))
    return pixels

MIN = {6:17, 7:9, 11:4, 12:29, 13:17, 14:16, 15:27, 16:1, 17:25, 21:7, 24:13, 29:2, 30:8, 31:1, 32:4, 33:5, 34:4, 35:8,
       38:25, 39:14, 40:18, 41:8, 42:5, 44:4, 46:4, 47:11, 48:2, 49:3, 52:3, 54:7, 55:1, 56:1, 57:12, 58:4, 64:10, 66:1}
null = {8,9,18,19,22,43,45,62,63}
cases = [(t, 0, 0) for t in range(67) if t not in null]
cases += [(11,1,0),(13,1,0),(15,1,0),(15,2,0),(15,3,0),(17,1,0),(33,1,0),(54,3,0),(54,0,2),(54,3,2),(56,0,2),(66,0,2)]
cases=[dict(type=t,mode=m,external=e) for t,m,e in cases]
def static_values(flags,minor=3,preset=None):
    result=[float(flags)]
    for bit,n in ((1,9),(16,17),(128,17),(256,17),(64,17),(2,16),(4,27 if minor>=2 else 25),(8,29 if minor>=2 else 26)):
        if not flags&bit:continue
        group=[.2]*n
        if bit in (16,128,256):group[8]=0.
        if bit==8:group[25]=0.
        if bit==2:group[12:16]=[1.,.2,.1,1.]
        result+=group
    if preset is not None:result+=list(preset)
    return result
cases += [dict(type=16,mode=0,external=0,floats=static_values(flags),label=f'flags{flags}') for flags in range(512)]
cases += [dict(type=16,mode=0,external=0,floats=static_values(flags,0),minor=0,label=f'legacy_flags{flags}') for flags in (4,8,6,14,511)]
for flags in (0,1,2,3,4,6,8,9,11,12,14,16,17,19,20,22,66,256,257,259,260,262):
    cases.append(dict(type=16,mode=0,external=0,floats=static_values(flags,preset=(1,0)),label=f'preset_flags{flags}'))
# Exercise every metal/shiny grade and glow selection, including the grade's
# own glow (index 4), plus all twelve dissolve preset variants.
for flags,grades,glows in ((1,20,1),(3,20,5),(4,8,1),(6,8,5),(2,1,5)):
    for grade in range(1,grades+1):
        for glow in range(glows):
            cases.append(dict(type=16,mode=0,external=0,floats=static_values(flags,preset=(grade,glow)),
                              label=f'preset_flags{flags}_grade{grade}_glow{glow}'))
for flags,grade,glow in ((8,1,0),(11,20,4),(14,8,4),(24,1,0)):
    for variant in range(12):
        values=static_values(flags,preset=(grade,glow));start=len(values)-31
        values[start+21]=float(np.float32((0,.3,.9)[variant//4]))
        values[start+22]=float((variant//2)%2)
        values[start+17:start+21]=[.2 if variant%2 else 1.]*4
        cases.append(dict(type=16,mode=0,external=0,floats=values,label=f'dissolve_preset_flags{flags}_variant{variant}'))
cases += [dict(type=t,mode=0,external=0,channel=c,label=f'font{c}') for c in range(1,5) for t in range(67) if t not in null]
cases += [dict(type=21,mode=0,external=0,floats=[.2]*6+[0.],label='normal_blur'),
          dict(type=54,mode=0,external=2,facing=2,label='linear_dodge'),
          dict(type=56,mode=0,external=2,stone=1,label='stone_mode1')]
for slot in (1,2,3):
    d=[1.]*4+[0.]*20;d[6]=.9;d[7]=.1;d[8]=.2;d[17]=.1;d[20]=d[22]=0.;d[21]=float(bool(slot&1));d[23]=float(bool(slot&2))
    cases.append(dict(type=36,mode=0,external=0,bits=list(struct.unpack('<24I',struct.pack('<24f',*d))),label=f'gauge_slots{slot}'))
xx, yy = np.meshgrid(np.arange(16), np.arange(16))
source = np.stack((xx*17, yy*17, ((xx+yy)%2)*255, np.where((xx//4)%2,128,255)), -1).astype(np.uint8)
color = (.8,.3,.5,.7)
results = []
start = time.perf_counter()
folder = ROOT / 'validation' / 'shader_pixels'
folder.mkdir(exist_ok=True)
sys.path.insert(0,str(OLD))
from test_rep_protocol_versions import pack_replay
replay_fixture=folder/'shader_fixture.rep'
replay_fixture.write_bytes(pack_replay(1.7,{},resources=['sprite/shader/mask.img']))
server=subprocess.Popen([str(ROOT/'build'/'rep_gpu.exe'),'--shader-server',str(replay_fixture)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf8')
for number, case in enumerate(cases):
    kind,mode,external=case['type'],case['mode'],case['external'];channel=case.get('channel',0);minor=case.get('minor',3)
    native_type=kind
    bound_frames=[]
    floats = [.2] * MIN.get(kind, 6)
    if kind == 16:
        floats = [0.]
    if kind == 58:
        floats = [0.,0.,16.,16.]
    if kind == 54 and mode == 3:
        floats[5] = 3.
    if kind == 40:
        floats[13],floats[16],floats[17] = 100.,20.,100.
    floats=case.get('floats',floats)
    bits = [0] * 6
    bits[5] = struct.unpack('<I', struct.pack('<f', mode))[0]
    bits=case.get('bits',bits)
    context = {'vertex_color': color, 'canvas': source, 'texture_resolver': resolve,
               'texture_dimensions': (16,16), 'replay_version':1.4, 'replay_minor':minor,
               'native_actor_facing':case.get('facing',0),'native_stone_mode':case.get('stone',0),
               'native_multitexture_color':(1.,.5,.25,1.)}
    if external:
        context['native_texture_bindings'] = {'t1': source}
    if channel:context.update(texture_channel=channel,font_texture_channel=channel)
    if kind==36 and len(bits)==24:context.update(resource_paths={0:'sprite/shader/mask.img'},resource_resolver=lambda path,index:full_texture(assets.native_frame(path,index)))
    fixture = folder / f'{number:03d}_type{kind}_mode{mode}.bin'
    output = fixture.with_suffix('.rgba')
    fixture.write_bytes(b'SFX1' + struct.pack('<11I', kind, 14, minor, 16, 16, channel, len(floats), len(bits), external, case.get('facing',0), case.get('stone',0))
                       + struct.pack('<4f', *color) + struct.pack('<'+'f'*len(floats), *floats) + struct.pack('<'+'I'*len(bits), *bits))
    try:
        server.stdin.write(str(fixture)+'\n'+str(output)+'\n');server.stdin.flush()
        native=json.loads(server.stdout.readline());native_program=native['program']
        expected = np.asarray(apply_effect(source, Effect(kind, tuple(floats), tuple(bits)), context=context))
        got = np.frombuffer(output.read_bytes(),np.uint8).reshape(16,16,4)
        difference = np.abs(got.astype(int)-expected.astype(int))
        row = {'type':kind, 'mode':mode, 'external':external,'channel':channel,'label':case.get('label','base'), **native,
               'maximum_channel_error':int(difference.max()), 'mean_channel_error':float(difference.mean()),
               'fraction_over_two':float((difference>2).mean())}
        Image.fromarray(expected).save(fixture.with_suffix('.oracle.png'))
        Image.fromarray(got).save(fixture.with_suffix('.gpu.png'))
        row['pass'] = row['maximum_channel_error'] <= 2
    except Exception as exc:
        row = {'type':kind, 'mode':mode, 'external':external, 'error':str(exc), 'pass':False}
    results.append(row)
    print(json.dumps(row), flush=True)
server.stdin.close();server.wait();assert server.returncode==0,server.stderr.read()
report = {'cases':len(results),'passed':sum(r['pass'] for r in results),'seconds':time.perf_counter()-start,
          'acceptance':'maximum absolute RGBA8 channel error <= 2 for every pixel',
          'records':results}
(ROOT/'validation'/'shader_pixel_differential.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({k:v for k,v in report.items() if k!='records'}))
raise SystemExit(any(not r['pass'] for r in results))
