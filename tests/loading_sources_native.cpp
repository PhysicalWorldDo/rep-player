#include "engine.hpp"
#include <iostream>

using namespace rep;
static void require(bool ok,const char* message){if(!ok)throw Error(message);}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=6)return 2;std::wstring mode=argv[1];std::filesystem::path packs=argv[2],replayPath=argv[3],shaderCache=argv[4],cache=argv[5];
    const std::string logical="sprite/source/frame.img";
    Replay replay(replayPath);Scene scene;require(replay.next(scene),"fixture scene missing");
    Gpu gpu(shaderCache);Assets assets(packs,true);Assets observer(assets.index());
    require(assets.frame(logical,0,0,false)->cropped().at(2)==255,"fixture must initially select blue patch frame0");
    Executor actual(gpu,assets,cache);actual.attach(replay);actual.setTransparent(true);
    bool triggered=false,committed=false;int retries=0,attempts=0;
    auto gate=[&]{if(mode==L"gate"&&!triggered){triggered=true;observer.frame(logical,1,0,false);}return true;};
    for(;attempts<3&&!committed;++attempts){
        try{committed=actual.executeSelected(scene,{},gate);}
        catch(const Error& error){if(std::string(error.what()).find("image sources changed")==std::string::npos)throw;++retries;}
    }
    require(committed,"selected scene failed to commit after source-stable retries");
    auto pixels=gpu.readback(actual.output());auto actualCrc=crc(pixels);
    if(mode==L"gate")require(triggered,"committing callback did not trigger shared Index rejection");
    Assets official(packs,false);Executor reference(gpu,official,cache/L"reference");reference.attach(replay);reference.setTransparent(true);reference.executeSelected(scene);auto expected=crc(gpu.readback(reference.output()));
    std::cout<<"{\"mode\":\""<<utf8(mode)<<"\",\"actual_crc\":"<<actualCrc<<",\"official_crc\":"<<expected<<",\"retries\":"<<retries<<",\"attempts\":"<<attempts<<",\"source_revision\":"<<assets.sourceRevision()<<"}\n";
    require(actualCrc==expected,mode==L"gate"?"commit installed a frame from rejected patch source":"one selected scene mixed rejected patch frame0 with official frame1");return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
