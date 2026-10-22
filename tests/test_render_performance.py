"""Count real work at renderer boundaries and verify its resulting pixels."""
from pathlib import Path
import subprocess
import sys
import unittest
import uuid

sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).parent))
from test_npk_priority import image,write_package,RED,GREEN,BLUE
ROOT=Path(__file__).resolve().parents[1]

class RenderPerformanceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=ROOT/'validation/render_optimization_20261022'/('contracts_'+uuid.uuid4().hex[:8])
        cls.folder.mkdir(parents=True)
        cls.packs=cls.folder/'ImagePacks2';cls.packs.mkdir()
        write_package(cls.packs/'sprite_test.NPK',{'sprite/test/frame.img':image(RED,BLUE)})
        write_package(cls.packs/'!patch.NPK',{'sprite/test/frame.img':image(GREEN)})
        write_package(cls.packs/'interface.NPK',{'sprite/interface/base.img':image(*([RED]*92))})
        # Instrument existing API boundaries, without requiring test-only product APIs.
        counters='''#pragma once
#include <d3d11.h>
#include <cstdint>
extern uint64_t measuredDecodeCalls,measuredVertexDiscards,measuredTextureUpdates;
inline HRESULT measuredMap(ID3D11DeviceContext* context,ID3D11Buffer* buffer,UINT sub,D3D11_MAP how,UINT flags,D3D11_MAPPED_SUBRESOURCE* data){if(how==D3D11_MAP_WRITE_DISCARD)measuredVertexDiscards++;return context->Map(buffer,sub,how,flags,data);}
'''
        (cls.folder/'counters.hpp').write_text(counters,encoding='utf-8')
        resources=(ROOT/'src/resources.cpp').read_text(encoding='utf-8')
        needle='std::shared_ptr<Frame> Assets::decodeFrame(const std::string& name,int index,int palette){'
        assert resources.count(needle)==1
        resources=resources.replace(needle,needle+'measuredDecodeCalls++;')
        (cls.folder/'resources.cpp').write_text('#include "counters.hpp"\n'+resources,encoding='utf-8')
        gpu=(ROOT/'src/gpu.cpp').read_text(encoding='utf-8')
        needle='context_->Map(vertices_.get(),0,'
        assert gpu.count(needle)==1
        gpu=gpu.replace(needle,'measuredMap(context_.get(),vertices_.get(),0,')
        needle='context_->UpdateSubresource(it->second->texture.get(),0,nullptr,pixels->rgba.data(),pixels->width*4,0);'
        assert gpu.count(needle)==1
        gpu=gpu.replace(needle,'measuredTextureUpdates++;'+needle)
        (cls.folder/'gpu.cpp').write_text('#include "counters.hpp"\n'+gpu,encoding='utf-8')
        compiler=ROOT/'toolchain/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
        cls.exe=cls.folder/'contracts.exe'
        command=[compiler,'-std=c++20','-O2','-DNOMINMAX','-DUNICODE','-D_UNICODE','-municode','-static','-I',ROOT/'src','-I',ROOT/'vendor/zlib','-I',ROOT/'vendor/freetype/include',ROOT/'tests/render_performance_native.cpp',cls.folder/'resources.cpp',cls.folder/'gpu.cpp',ROOT/'src/fonts.cpp',ROOT/'build/protocol.o',ROOT/'vendor/freetype/libfreetype.a',ROOT/'vendor/zlib/libz.a','-ld3d11','-ldxgi','-ld3dcompiler','-ldxguid','-o',cls.exe]
        p=subprocess.run(list(map(str,command)),capture_output=True,timeout=120)
        (cls.folder/'build.log').write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError(p.stderr.decode(errors='replace'))
        print('RENDER_CONTRACTS='+str(cls.folder),flush=True)

    def run_contract(self,mode):
        p=subprocess.run([str(self.exe),mode,str(ROOT/'assets/shaders'),str(self.packs)],capture_output=True,timeout=90)
        (self.folder/(mode+'.log')).write_bytes(p.stdout+p.stderr)
        # Show failures without printing the many repeated-pixel assertions.
        failures=[line for line in (p.stdout+p.stderr).decode(errors='replace').splitlines() if line.startswith('FAIL') or line.startswith('failures')]
        self.assertEqual(p.returncode,0,'\n'.join(failures) or p.stderr.decode(errors='replace'))

    def test_static_frame_cache(self):self.run_contract('cache')
    def test_vertex_stream(self):self.run_contract('vertices')
    def test_font_upload_versions(self):self.run_contract('fonts')

if __name__=='__main__':unittest.main()
