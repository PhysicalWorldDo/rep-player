#include "protocol.hpp"
#include <functional>
#include <future>
#include <iostream>
#include <type_traits>

static void require(bool value,const char* message){if(!value)throw rep::Error(message);}

// The fallback lets the unchanged implementation run and fail the behavior
// contract, rather than making the RED result a missing-constructor error.
template<class T> std::unique_ptr<T> forkReplay(const T& source,const std::filesystem::path& path){
    if constexpr(std::is_copy_constructible_v<T>)return std::make_unique<T>(source);
    else return std::make_unique<T>(path,source.options);
}
template<class T> std::unique_ptr<T> openCancelled(const std::filesystem::path& path,const std::function<bool()>& cancelled){
    if constexpr(std::is_constructible_v<T,const std::filesystem::path&,rep::ReplayOptions,const std::function<bool()>&>)
        return std::make_unique<T>(path,rep::ReplayOptions{},cancelled);
    else return std::make_unique<T>(path);
}
static void checkScene(const rep::Scene& scene,int ordinal,int timestamp){
    require(scene.ordinal==uint64_t(ordinal)&&scene.timestamp==timestamp,"independent scene cursor changed");
    require(scene.ids==std::vector<uint32_t>{7},"scene references changed");
    require(scene.aux==rep::Bytes({1,2,3,4}),"scene auxiliary bytes changed");
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=3)return 2;std::wstring_view mode=argv[1];std::filesystem::path path=argv[2];
    if(mode==L"cancel-before"||mode==L"cancel-dictionary"){
        size_t polls=0;bool rejected=false;
        const size_t target=mode==L"cancel-before"?1:30;
        try{auto replay=openCancelled<rep::Replay>(path,[&]{return ++polls>=target;});}
        catch(const rep::Error& error){rejected=std::string(error.what()).find("cancelled")!=std::string::npos;}
        require(rejected,"REP construction ignored cancellation");
        require(polls>=target,"cancellation did not reach requested loading phase");
        rep::Replay fresh(path);auto result=fresh.validate();require(result.scenes==3&&result.exactEof,"cancelled construction affected later opens");
        std::cout<<"{\"cancelled\":true,\"polls\":"<<polls<<"}\n";return 0;
    }
    auto source=std::make_unique<rep::Replay>(path);rep::Scene scene;require(source->next(scene),"fixture has no first scene");checkScene(scene,0,0);
    auto first=forkReplay(*source,path),second=forkReplay(*source,path);
    if(mode==L"shared"){
        require(&first->dictionary==&source->dictionary,"fork decoded a second dictionary");
        require(&first->resources==&source->resources&&&first->resourceBytes==&source->resourceBytes,"fork copied resource storage");
        require(first->command(7)==source->command(7)&&second->command(7)==source->command(7),"command storage was not shared");
        require(&first->header==&source->header,"fork copied header storage");
    }
    source.reset();
    require(first->next(scene),"fork lost retained timeline");checkScene(scene,0,0);
    require(second->next(scene),"second fork lost timeline");checkScene(scene,0,0);
    require(second->next(scene),"second fork has no next scene");checkScene(scene,1,17);
    require(first->next(scene),"first fork has no independent next scene");checkScene(scene,1,17);
    first->rewind();require(first->next(scene),"rewind failed");checkScene(scene,0,0);
    require(second->next(scene),"rewinding one cursor changed another");checkScene(scene,2,33);
    require(!second->next(scene),"unexpected fourth scene");
    auto a=std::async(std::launch::async,[&]{return first->validate(true);});
    auto b=std::async(std::launch::async,[&]{return second->validate(true);});
    auto as=a.get(),bs=b.get();require(as.scenes==3&&as.references==3&&as.auxBytes==12&&as.exactEof,"shared validation changed timeline");
    require(as.timelineCrc==bs.timelineCrc&&as.timestamps==bs.timestamps,"parallel cursors differ");
    require(first->path(0)=="sprite/shared.img"&&first->command(7)->instructions[0].opcode==3,"retained dictionary/resources invalid after source destruction");
    std::cout<<"{\"shared\":true,\"scenes\":"<<as.scenes<<",\"timeline_crc\":"<<as.timelineCrc<<"}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
