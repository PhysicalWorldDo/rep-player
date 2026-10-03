#pragma once
#include "gpu.hpp"
#include <map>
#include <tuple>
namespace rep {
struct ShaderContext {
    int width=0,height=0,version=14,minor=3,channel=0;
    float x=0,y=0,scaleX=1,scaleY=1;
    int direction=1;
    bool imagePresent=false,directSource=false;
    float cacheScale=0,cacheX=0,cacheY=0;
    std::shared_ptr<GpuTexture> canvas;
    std::array<std::shared_ptr<GpuTexture>,4> external{};
    int actorFacing=0,stoneMode=0;
    std::array<float,4> multiColor{};
};
class Binder {
    Gpu& gpu_;Assets& assets_;Replay* replay_;
    std::unordered_map<uint64_t,std::shared_ptr<Frame>> builtin_;
    using CacheKey=std::tuple<const Effect*,const GpuTexture*,int,int,int,int,bool>;
    std::map<CacheKey,Material> materials_;
    uint32_t randomState_=1;
    std::shared_ptr<Frame> builtin(int frame,int group=0);
    Material bindUncached(const Effect* effect,std::shared_ptr<GpuTexture> source,ShaderContext context);
public:
    Binder(Gpu& gpu,Assets& assets,Replay* replay=nullptr):gpu_(gpu),assets_(assets),replay_(replay){}
    void setReplay(Replay* replay){replay_=replay;materials_.clear();randomState_=1;}
    uint32_t randomState()const{return randomState_;}
    void restoreRandomState(uint32_t state){randomState_=state;}
    Material bind(const Effect* effect,std::shared_ptr<GpuTexture> source,ShaderContext context);
};
int programId(std::string_view name);
std::vector<ImageInput> shaderImageInputs(const Effect* effect,const Replay& replay);
}
