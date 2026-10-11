#include "export.hpp"
#include <iostream>

int wmain(int argc,wchar_t** argv){try{
    if(argc!=6)throw rep::Error("Expected root, client, replay, output name and policy case");
    std::filesystem::path root=argv[1],client=argv[2],replay=argv[3];std::wstring mode=argv[5];
    rep::ExportOptions options;options.format=rep::ExportFormat::Png;options.fps=30;options.audio=false;options.outputDirectory=root/L"exports";options.fileName=argv[4];
    auto& policy=options.imageVisibility;
    const std::string monster="sprite/monster/test/body.img",person="sprite/character/test/body.img",effect="sprite/monster/test/effect/flash.img",other="sprite/custom/test/frame.img";
    if(mode==L"categories"||mode==L"manual"||mode==L"snapshot")policy.hideBackground=policy.hideMonsters=policy.hideCharacters=true;
    if(mode==L"manual"){policy.setOverride(monster,true);policy.setOverride(effect,false);}
    if(mode==L"legacy"||mode==L"legacy_override")options.hiddenImages.insert(other);
    if(mode==L"legacy_override")policy.setOverride(other,true);
    if(mode==L"snapshot"){
        auto preview=policy;preview.showAll();
        if(!policy.hidden(person)||preview.hidden(person))throw rep::Error("Export options must hold their own policy snapshot");
    }
    rep::Gpu gpu(root/L"assets"/L"shaders");rep::Assets assets(client/L"ImagePacks2");
    auto result=rep::exportReplay(gpu,assets,client,root/L"runtime"/L"cache",replay,options);
    std::cout<<"{\"frames\":"<<result.frames<<",\"scenes\":"<<result.executedScenes<<",\"hidden\":"<<result.hiddenImageCount<<"}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
