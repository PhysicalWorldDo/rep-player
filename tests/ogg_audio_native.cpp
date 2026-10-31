#include "../src/audio.hpp"
#include <cmath>
#include <iostream>
#include <type_traits>
#include <thread>

static void paths(const std::vector<std::string>& values){
    std::cout<<'[';bool first=true;for(const auto& value:values){if(!first)std::cout<<',';first=false;std::cout<<'"'<<value<<'"';}std::cout<<']';
}
template<class Track,class Player>
int probe(const std::filesystem::path& client,const std::filesystem::path& replayPath,const std::wstring& mode){
    if constexpr(requires(Track& t,Player& p){t.oggPaths();t.oggPaths(uint64_t(0));t.setHiddenOggs(std::unordered_set<std::string>{});t.oggClip(std::string{});p.oggPaths();p.setHiddenOggs(std::unordered_set<std::string>{});p.oggClip(std::string{});p.durationMilliseconds();}&&std::is_constructible_v<Track,std::shared_ptr<const rep::AudioClip>>){
        rep::Replay replay(replayPath);auto owned=std::make_unique<Track>(client,client/L"cache",replay);auto& track=*owned;
        if(mode==L"paths"){
            std::cout<<"{\"implemented\":true,\"all\":";paths(track.oggPaths());
            for(uint64_t ordinal=0;ordinal<4;ordinal++){std::cout<<",\"frame"<<ordinal<<"\":";paths(track.oggPaths(ordinal));}
            std::cout<<",\"decoders\":"<<track.decoderProcesses()<<"}";return 0;
        }
        if(mode==L"player"){
            Player player(std::move(owned));player.volume(0,true);player.setHiddenOggs({"Music/First.ogg"});auto serial=player.seek(10,false);
            for(int n=0;n<1000&&player.available()&&player.pcmReadySerial()<serial;n++)std::this_thread::sleep_for(std::chrono::milliseconds(2));
            auto clip=player.oggClip("music/first.ogg");std::cout<<"{\"implemented\":true,\"ready\":"<<(player.pcmReadySerial()>=serial?"true":"false")<<",\"clip\":"<<(clip?"true":"false")<<",\"all\":";paths(player.oggPaths());std::cout<<"}";return 0;
        }
        if(mode==L"solo"){
            track.setHiddenOggs({"music/first.ogg"});auto clip=track.oggClip("/MUSIC\\FIRST.OGG");
            auto cancelled=track.oggClip("music/first.ogg",[]{return true;});auto unknown=track.oggClip("music/unused.ogg");auto missing=track.oggClip("music/missing.ogg");
            Track solo(clip);solo.seek(0);std::vector<float> out(size_t(clip->frames()+100)*2);solo.render(out);
            double error=0;for(size_t n=0;n<clip->samples.size();n++)error=std::max(error,double(std::abs(out[n]-clip->samples[n])));
            double tail=0;for(size_t n=clip->samples.size();n<out.size();n++)tail+=std::abs(out[n]);
            std::cout<<"{\"implemented\":true,\"error\":"<<error<<",\"tail\":"<<tail<<",\"cancelled\":"<<(!cancelled?"true":"false")<<",\"unknown\":"<<(!unknown?"true":"false")<<",\"missing\":"<<(!missing?"true":"false")<<",\"duration\":"<<solo.durationMilliseconds()<<"}";return 0;
        }
        auto samples=[&](bool hidden){track.setHiddenOggs(hidden?std::unordered_set<std::string>{"/MUSIC\\FIRST.OGG"}:std::unordered_set<std::string>{});track.seek(0);std::vector<float> out(4800*2);track.render(out);return out;};
        auto baseline=samples(false),filtered=samples(true);
        track.setHiddenOggs({"music/first.ogg"});track.seek(0);std::vector<float> first(1440*2),rest(3360*2);track.render(first);track.setHiddenOggs({});track.render(rest);
        double resumedError=0;for(size_t n=0;n<rest.size();n++)resumedError=std::max(resumedError,double(std::abs(rest[n]-baseline[first.size()+n])));
        std::cout<<"{\"implemented\":true,\"resumedError\":"<<resumedError<<",\"events\":"<<track.eventCount()<<",\"before\":[";
        for(int ms=0;ms<100;ms++){if(ms)std::cout<<',';std::cout<<baseline[ms*96];}std::cout<<"],\"after\":[";
        for(int ms=0;ms<100;ms++){if(ms)std::cout<<',';std::cout<<filtered[ms*96];}std::cout<<"]}";return 0;
    }else{std::cout<<"{\"implemented\":false}";return 0;}
}
int wmain(int argc,wchar_t** argv){try{if(argc!=4)return 2;return probe<rep::AudioTrack,rep::AudioPlayer>(argv[1],argv[2],argv[3]);}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
