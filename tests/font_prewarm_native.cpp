#include "fonts.hpp"
#include <atomic>
#include <iostream>

std::atomic<uint64_t> measuredAtlasConstructions=0;
using namespace rep;
static void require(bool ok,const char* message){if(!ok)throw Error(message);}
template<class T>concept GlyphPrewarm=requires(T& fonts,const Instruction& instruction,const Replay& replay){fonts.prewarmGlyphs(instruction,replay);};
template<class T>void prewarm(T& fonts,const Instruction& instruction,const Replay& replay){
    if constexpr(GlyphPrewarm<T>)fonts.prewarmGlyphs(instruction,replay);
    else require(false,"glyph-only Fonts prewarm API is absent");
}
static bool equal(const std::vector<GlyphQuad>& a,const std::vector<GlyphQuad>& b){
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i){const auto& x=a[i];const auto& y=b[i];
        if(x.x!=y.x||x.y!=y.y||x.scale!=y.scale||x.color!=y.color||x.lower!=y.lower||x.channel!=y.channel||x.gradient!=y.gradient)return false;
        if(x.frame->width!=y.frame->width||x.frame->height!=y.frame->height||x.frame->rect!=y.frame->rect)return false;
        if(x.frame->texture->width!=y.frame->texture->width||x.frame->texture->height!=y.frame->texture->height||x.frame->texture->rgba!=y.frame->texture->rgba)return false;
    }
    return true;
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=3)return 2;ReplayOptions options;options.resourceCodePage=65001;Replay replay(argv[1],options);std::filesystem::path fontsRoot=argv[2];
    std::array<std::array<int32_t,11>,3> cases{{
        {18,400,2,0,2,1,0,int32_t(0xffff0000),int32_t(0xff000000),int32_t(0xff0000ff),0x101},
        {11,400,0,0,2,1,0,int32_t(0xffffffff),int32_t(0xff000000),0,1},
        {26,700,3,5,3,4,-500,int32_t(0xff00ff00),int32_t(0xff000000),0,0x1010001}
    }};
    for(const auto& parameters:cases){
        Instruction instruction;instruction.opcode=48;instruction.resource=0;instruction.native.resize(sizeof(parameters));std::memcpy(instruction.native.data(),parameters.data(),sizeof(parameters));
        Fonts cold(fontsRoot);auto expected=cold.prepare(instruction,replay,4,5);require(!expected.empty(),"cold glyph reference must draw text");
        auto cache=std::make_shared<RetainedCache>(64*1024*1024);auto before=measuredAtlasConstructions.load();
        {Fonts warming(fontsRoot,cache);prewarm(warming,instruction,replay);}
        require(measuredAtlasConstructions.load()==before,"glyph prewarm created an unused Atlas/Pixels");
        size_t waits=0;Fonts prepared(fontsRoot,cache);prepared.setDemandWaitCallback([&]{++waits;});auto actual=prepared.prepare(instruction,replay,4,5);
        require(waits==0,"post-prewarm prepare still had a glyph/font file miss");
        require(equal(expected,actual),"prewarmed text quads/atlas coverage differ from cold prepare");
    }
    std::cout<<"{\"cases\":"<<cases.size()<<",\"noWarmupAtlas\":true,\"allPrepareHits\":true,\"quadsAndCoverageEqual\":true}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
