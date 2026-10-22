#include "fonts.hpp"
#include "gpu.hpp"
#include <iostream>

uint64_t measuredDecodeCalls=0,measuredVertexDiscards=0,measuredTextureUpdates=0;
namespace {
int failures=0;
void expect(bool value,const char* text){std::cout<<(value?"PASS ":"FAIL ")<<text<<'\n';failures+=!value;}
std::array<uint8_t,4> pixel(const std::shared_ptr<rep::Frame>& frame){auto b=frame->cropped();return {b[0],b[1],b[2],b[3]};}
void cache(const std::filesystem::path& packs){
    const std::string path="sprite/test/frame.img";rep::Assets assets(packs);std::vector<std::pair<std::string,int>> observed;
    assets.setFrameObserver([&](const std::string& p,int i){observed.emplace_back(p,i);});
    auto initial=measuredDecodeCalls;
    for(int n=0;n<500;n++)expect(pixel(assets.frame(path,0))==std::array<uint8_t,4>{0,255,0,255},"cached patch pixels");
    expect(measuredDecodeCalls-initial==1,"repeated frame resolves only once");
    expect(observed.size()==500&&observed.back()==std::pair{path,0},"cache hits preserve observation events");
    expect(assets.decodedFrames==500,"cache hits preserve decoded-frame accounting");
    rep::Assets peer(assets.index());expect(pixel(peer.frame(path,1))==std::array<uint8_t,4>{0,0,255,255},"missing patch frame falls back to original");
    expect(pixel(assets.frame(path,0))==std::array<uint8_t,4>{255,0,0,255},"shared patch rejection invalidates a cached frame");
    initial=measuredDecodeCalls;assets.frame(path,0);expect(measuredDecodeCalls==initial,"new source stays cached");
    assets.clearDecodedImages();assets.frame(path,0);expect(measuredDecodeCalls>initial,"clearing decoded images invalidates resolved frames");
    observed.clear();initial=measuredDecodeCalls;
    for(int n=0;n<5;n++)assets.frame("sprite/test/absent.img",0);
    expect(measuredDecodeCalls-initial==1,"missing-resource fallback resolves once");
    expect(assets.fallbacks==5&&observed.size()==10,"fallback hits preserve counts and both notifications");
    bool rejected=false;try{assets.frame("sprite/test/absent.img",0,0,false);}catch(const rep::Error&){rejected=true;}
    expect(rejected,"strict requests cannot reuse a missing-resource fallback");
    auto lower=assets.frame(path,0);auto upper=assets.frame("SPRITE\\TEST\\FRAME.IMG",0);
    expect(lower->actualPath==path&&upper->actualPath==path,"alternate spellings retain canonical actual paths");
}
rep::Bytes readTexture(rep::Gpu& gpu,const std::shared_ptr<rep::GpuTexture>& image){
    D3D11_TEXTURE2D_DESC d{};image->texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    rep::Com<ID3D11Texture2D> copy;rep::check(gpu.device()->CreateTexture2D(&d,nullptr,copy.out()),"test readback allocation");gpu.context()->CopyResource(copy.get(),image->texture.get());
    D3D11_MAPPED_SUBRESOURCE map{};rep::check(gpu.context()->Map(copy.get(),0,D3D11_MAP_READ,0,&map),"test readback map");rep::Bytes bytes(size_t(d.Width)*d.Height*4);
    for(UINT y=0;y<d.Height;y++)std::memcpy(bytes.data()+size_t(y)*d.Width*4,static_cast<uint8_t*>(map.pData)+size_t(y)*map.RowPitch,d.Width*4);
    gpu.context()->Unmap(copy.get(),0);return bytes;
}
void vertices(rep::Gpu& gpu){
    auto target=gpu.target(64,64,false);float clear[4]{};gpu.begin(target,clear);
    auto white=std::make_shared<rep::Pixels>();white->width=white->height=1;white->rgba={255,255,255,255};
    rep::Material material;material.program=4;material.blend=0x8000;material.textures[0]=gpu.texture(white);
    material.constants[0]=material.constants[1]=1;material.constants[2]=material.constants[3]=.5f;
    rep::Bytes expected(64*64*4);const uint64_t start=measuredVertexDiscards;constexpr int count=24000;
    for(int i=0;i<count;i++){
        int cell=i%(64*64),x=cell%64,y=cell/64;std::array<uint8_t,4> color{uint8_t(i%251),uint8_t(i/251%251),uint8_t(i/701%251),255};
        std::copy(color.begin(),color.end(),expected.begin()+cell*4);
        std::array<rep::Vertex,6> v{};int corners[]={0,1,2,2,1,3};
        for(int n=0;n<6;n++){auto c=corners[n];v[n].position[0]=float(x+(c&1));v[n].position[1]=float(y+(c>>1));for(int k=0;k<4;k++)v[n].color[k]=color[k]/255.f;v[n].texcoord[0][0]=v[n].texcoord[0][1]=.5f;v[n].texcoord[2][0]=1;v[n].texcoord[7][0]=v[n].texcoord[7][1]=1;}
        gpu.draw(v,material,{0,0,64,64});gpu.flush();
        if(i==12000){gpu.clearStencil(target);gpu.begin(target);}
    }
    expect(gpu.readback(target)==expected,"vertex append and wrap preserve every rasterized pixel");
    expect(measuredVertexDiscards-start<=4,"small draws discard only when the vertex stream fills");
}
void fonts(rep::Gpu& gpu){
    rep::Atlas atlas;rep::Glyph glyph;glyph.width=glyph.height=3;glyph.coverage.assign(9,255);rep::Glyph blank;
    atlas.registerGlyph("a",glyph,blank,1,1);atlas.refresh();auto texture=gpu.texture(atlas.pixels,true);auto initial=measuredTextureUpdates;
    for(int n=0;n<20;n++){atlas.refresh();gpu.texture(atlas.pixels,true);}
    expect(measuredTextureUpdates==initial,"unchanged font atlas performs no GPU updates");
    atlas.registerGlyph("b",glyph,blank,1,1);atlas.refresh();texture=gpu.texture(atlas.pixels,true);
    expect(measuredTextureUpdates==initial+1,"new glyph uploads changed atlas once");
    expect(readTexture(gpu,texture)==atlas.pixels->rgba,"new glyph reaches GPU pixels");
    auto unmanaged=std::make_shared<rep::Pixels>();unmanaged->width=unmanaged->height=1;unmanaged->rgba={1,2,3,4};auto dynamic=gpu.texture(unmanaged,true);
    initial=measuredTextureUpdates;unmanaged->rgba={5,6,7,8};gpu.texture(unmanaged,true);
    expect(measuredTextureUpdates==initial+1&&readTexture(gpu,dynamic)==unmanaged->rgba,"unversioned dynamic video pixels still upload");
    rep::Atlas growing;rep::Glyph large;large.width=large.height=1022;large.coverage.assign(1022*1022,170);
    growing.registerGlyph("large0",large,blank,1,1);growing.refresh();gpu.texture(growing.pixels,true);
    growing.registerGlyph("large1",large,blank,1,1);growing.refresh();gpu.texture(growing.pixels,true);
    growing.registerGlyph("large2",large,blank,1,1);growing.refresh();auto grown=gpu.texture(growing.pixels,true);
    expect(grown->width==2048&&readTexture(gpu,grown)==growing.pixels->rgba,"atlas growth recreates texture with all old and new pixels");
}
}
int wmain(int argc,wchar_t** argv){try{if(argc!=4)return 2;std::wstring mode=argv[1];if(mode==L"cache")cache(argv[3]);else{rep::Gpu gpu(argv[2]);if(mode==L"vertices")vertices(gpu);else if(mode==L"fonts")fonts(gpu);else return 2;}std::cout<<"failures "<<failures<<'\n';return failures?1:0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
