#include "../src/audio.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <thread>

static void paths(const std::vector<std::string>& values){std::cout<<'[';bool first=true;for(auto& p:values){if(!first)std::cout<<',';first=false;std::cout<<'"'<<p<<'"';}std::cout<<']';}
template<class Track,class Player>
int probe(const std::filesystem::path& client,const std::filesystem::path& replayPath,const std::wstring& mode,const std::wstring& query){
    if constexpr(requires(Track& t,Player& p){t.activeOggs();p.oggActivity().seekSerial;p.oggActivity().sample;p.oggActivity().milliseconds;p.oggActivity().ready;p.oggActivity().paths;}){
        rep::Replay replay(replayPath);auto track=std::make_unique<Track>(client,client/L"cache",replay);
        if(mode==L"track"||mode==L"hidden"){
            if(mode==L"hidden"){auto all=track->oggPaths();track->setHiddenOggs({all.begin(),all.end()});}
            std::wistringstream in(query);int64_t time;std::cout<<"{\"implemented\":true,\"states\":[";bool first=true;
            while(in>>time){if(!track->seek(time))return 3;if(!first)std::cout<<',';first=false;std::cout<<"{\"time\":"<<time<<",\"paths\":";paths(track->activeOggs());std::cout<<'}';}
            std::cout<<"]}";return 0;
        }
        if(mode==L"raw"){
            auto all=track->oggPaths();auto clip=track->oggClip(all.front());Track raw(clip);raw.seek(10);auto direct=raw.activeOggs();Player player(std::make_unique<Track>(clip));player.volume(0,true);auto serial=player.seek(10,false);
            for(int n=0;n<2000&&player.available()&&player.pcmReadySerial()<serial;n++)std::this_thread::sleep_for(std::chrono::milliseconds(1));
            std::cout<<"{\"implemented\":true,\"direct\":";paths(direct);std::cout<<",\"paths\":";paths(player.oggActivity().paths);std::cout<<"}";return 0;
        }
        Player player(std::move(track));player.volume(0,true);player.setRemainingPrefetchEnabled(false);if(!player.available())throw rep::Error(player.error());
        auto wait=[&](uint64_t serial){for(int n=0;n<2000&&player.available();n++){auto s=player.oggActivity();if(s.ready&&s.seekSerial==serial)return s;std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw rep::Error("Activity snapshot did not become ready");};
        if(mode==L"live"){
            player.setPlaybackRate(2);auto serial=player.seek(190,false);wait(serial);if(!player.resumePrepared(serial))throw rep::Error("Cannot resume prepared audio");
            std::cout<<"{\"implemented\":true,\"states\":[";bool first=true;for(int n=0;n<2000;n++){auto s=player.oggActivity();if(s.ready){if(!first)std::cout<<',';first=false;std::cout<<"{\"serial\":"<<s.seekSerial<<",\"sample\":"<<s.sample<<",\"time\":"<<s.milliseconds<<",\"paths\":";paths(s.paths);std::cout<<'}';if(s.milliseconds>=550)break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}player.pause();std::cout<<"]}";return 0;
        }
        std::wistringstream in(query);int64_t time;std::cout<<"{\"implemented\":true,\"states\":[";bool first=true;
        while(in>>time){auto serial=player.seek(time,false);auto s=wait(serial);if(!first)std::cout<<',';first=false;std::cout<<"{\"serial\":"<<s.seekSerial<<",\"expectedSerial\":"<<serial<<",\"sample\":"<<s.sample<<",\"time\":"<<s.milliseconds<<",\"buffered\":"<<player.stats().bufferedMilliseconds<<",\"paths\":";paths(s.paths);std::cout<<'}';}
        auto discarded=player.seek(210,false);auto latest=player.seek(900,false);auto s=wait(latest);
        std::cout<<"],\"latestSerial\":"<<latest<<",\"observedSerial\":"<<s.seekSerial<<",\"latestPaths\":";paths(s.paths);std::cout<<"}";return 0;
    }else{std::cout<<"{\"implemented\":false}";return 0;}
}
int wmain(int argc,wchar_t** argv){try{if(argc!=5)return 2;return probe<rep::AudioTrack,rep::AudioPlayer>(argv[1],argv[2],argv[3],argv[4]);}catch(const std::exception& error){std::cerr<<error.what();return 1;}}
