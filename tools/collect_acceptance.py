"""Collect existing native validation receipts without modifying client inputs."""
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'validation'
def read(name):return json.loads((OUT/name).read_text(encoding='utf8'))

protocol=read('protocol_library_differential.json')
representatives=read('representatives/summary.json')
pixels=read('shader_pixel_differential.json')
gpu=read('gpu_original_program_creation.json')
benchmark=read('ui_benchmark.json')
smoke=read('ui_smoke.json')
recovery=read('ui_recovery.json')
log=(OUT/'unittest_final.log').read_text(encoding='utf8')
units=int(re.search(r'Ran (\d+) tests',log).group(1))
assert log.strip().endswith('OK')
assert protocol['files']==protocol['exact_eof']==3064 and not protocol['failures']
assert representatives['files']==representatives['passed']==40 and representatives['scenes']==8174
assert pixels['cases']==pixels['passed'] and all(r['maximum_channel_error']<=2 for r in pixels['records'])
assert gpu['programs_created']==169 and gpu['hardware']
for report in (benchmark,smoke,recovery):
    assert report['pass'] and report['autoplay'] and report['switched'] and report['replayed'] and report['frozen']
assert recovery['recovered_after_scene_error']
assert benchmark['process_first_present_seconds']<2 and benchmark['first_open_seconds']<2 and benchmark['hot_replay_seconds']<2
assert abs(benchmark['first_playback_seconds']-benchmark['first_recorded_seconds'])<.1
assert abs(benchmark['hot_playback_seconds']-benchmark['first_recorded_seconds'])<.1

case_types=sorted({r['type'] for r in pixels['records']})
programs=sorted({r['program'] for r in pixels['records']})
report={
    'pass':True,
    'player':str(ROOT/'build'/'rep_player.exe'),
    'launcher':str(ROOT/'Start.cmd'),
    'readme':str(ROOT/'README.txt'),
    'runtime':'C++20 / D3D11; no Python playback dependency; audio disabled',
    'protocol':protocol,
    'real_gpu_consumption':{k:v for k,v in representatives.items() if k!='records'},
    'shader_pixels':{'cases':pixels['cases'],'passed':pixels['passed'],'pixel_output_types':len(case_types),
                     'types':case_types,'programs_executed':len(programs),'program_ids':programs,
                     'maximum_channel_error':max(r['maximum_channel_error'] for r in pixels['records']),
                     'acceptance':pixels['acceptance']},
    'shader_creation':gpu,
    'unittest':{'passed':units,'log':'unittest_final.log'},
    'ui':{'autoplay':True,'switch':True,'replay':True,'end_frame_frozen':True,
          'later_scene_error_recovery':True,'tree_files':benchmark['tree_replays']},
    'performance':benchmark,
    'timing_scope':{'process':'native application initialization through first Present, new process, OS disk cache not flushed',
                    'open':'REP selection through first Present',
                    'hot':'same-process replay with resource caches',
                    'frame':'CPU scene execution and Present wall time; not GPU timestamp queries'},
    'limits':['MaskBlur uses an explicit D3D11 vertex/geometry compatibility bridge and original PS.',
              'Small-texture padding is zero initialized here; native reused pool padding was not proven deterministic.',
              'No game launch or dynamic frame-by-frame game comparison was performed.'],
    'preservation':'Existing skill_player and D:/115us/client files are read-only; outputs stay in rep_player.'
}
(OUT/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'pass':True,'protocol_files':3064,'representative_files':40,'unit_tests':units,
                  'shader_cases':pixels['cases'],'maximum_channel_error':report['shader_pixels']['maximum_channel_error'],
                  'process_first_present_seconds':benchmark['process_first_present_seconds'],
                  'first_open_seconds':benchmark['first_open_seconds'],'hot_replay_seconds':benchmark['hot_replay_seconds'],
                  'recorded_seconds':benchmark['first_recorded_seconds'],'playback_seconds':benchmark['first_playback_seconds']}))
