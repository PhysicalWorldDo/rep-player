#include "bindings.hpp"
#include <iostream>

template<class T>static void transient(T& context){
    if constexpr(requires{context.transientSource=true;})context.transientSource=true;
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 2;
    rep::Assets assets(argv[1]);rep::Gpu gpu({});rep::Binder binder(gpu,assets);
    std::array<rep::Effect,3> effects;
    for(size_t n=0;n<effects.size();n++){
        auto& effect=effects[n];effect.present=true;effect.type=std::array{16,17,57}[n];
        effect.floats.resize(n==0?1:n==1?25:12);if(n==1)effect.floats[10]=-1;
    }
    rep::ShaderContext context;context.width=context.height=2;transient(context);
    std::vector<std::weak_ptr<rep::GpuTexture>> old;
    for(auto& effect:effects)for(int n=0;n<32;n++){
        auto pixels=std::make_shared<rep::Pixels>();pixels->width=pixels->height=2;pixels->rgba.resize(16,uint8_t(n));
        auto texture=gpu.texture(pixels);old.push_back(texture);
        {auto material=binder.bind(&effect,texture,context);}
        gpu.forgetTexture(pixels.get());
    }
    size_t retained=0;for(auto& texture:old)retained+=!texture.expired();
    auto pixels=std::make_shared<rep::Pixels>();pixels->width=pixels->height=2;pixels->rgba.resize(16,255);
    auto source=gpu.texture(pixels);std::weak_ptr<rep::GpuTexture> weak=source;bool cached=false;
    {auto first=binder.bind(&effects[0],source,{});auto second=binder.bind(&effects[0],source,{});cached=first.textures[1]==second.textures[1];}
    gpu.forgetTexture(pixels.get());source.reset();bool stableRetained=!weak.expired();
    binder.setReplay(nullptr);bool released=weak.expired();
    std::cout<<"{\"dynamic_retained\":"<<retained<<",\"static_cached\":"<<(cached&&stableRetained?"true":"false")<<",\"reset_releases\":"<<(released?"true":"false")<<"}";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
