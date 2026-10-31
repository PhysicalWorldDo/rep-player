#include "../src/audio.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <mutex>
#include <thread>

struct Submission {uint64_t serial;int64_t end;float peak;};
static std::mutex observationsMutex;
static std::unordered_map<const void*,std::vector<Submission>> observations;

// Only the generated validation copy of audio.cpp calls this observation point.
void observeOggSubmission(const void* track,uint64_t serial,int64_t end,const float* pcm,size_t count){
    float peak=0;for(size_t n=0;n<count;n++)peak=std::max(peak,std::abs(pcm[n]));
    std::lock_guard lock(observationsMutex);observations[track].push_back({serial,end,peak});
}
static std::vector<Submission> submitted(const void* track){std::lock_guard lock(observationsMutex);return observations[track];}
static void ready(rep::AudioPlayer& player,uint64_t serial){
    for(int n=0;n<2000&&player.available()&&player.pcmReadySerial()<serial;n++)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(!player.available()||player.pcmReadySerial()<serial)throw rep::Error("PCM submission did not become ready: "+player.error());
}
static void print(const char* name,const std::vector<Submission>& buffers){
    std::cout<<'"'<<name<<"\":[";bool first=true;for(auto& b:buffers){if(!first)std::cout<<',';first=false;std::cout<<"{\"serial\":"<<b.serial<<",\"end\":"<<b.end<<",\"peak\":"<<b.peak<<'}';}std::cout<<']';
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=3)return 2;rep::Replay replay(argv[2]);auto track=std::make_unique<rep::AudioTrack>(argv[1],std::filesystem::path(argv[1])/L"cache",replay);
    auto identity=track.get();rep::AudioPlayer player(std::move(track));if(!player.available())throw rep::Error(player.error());player.volume(0,true);player.setRemainingPrefetchEnabled(false);
    auto paths=player.oggPaths();player.setHiddenOggs({paths.begin(),paths.end()});auto hiddenSerial=player.seek(10,false);ready(player,hiddenSerial);
    auto hidden=submitted(identity);player.setHiddenOggs({});auto enabledSerial=player.seek(10,false);ready(player,enabledSerial);
    auto enabled=submitted(identity);auto clip=player.oggClip(paths.front());
    auto solo=std::make_unique<rep::AudioTrack>(std::move(clip));auto soloIdentity=solo.get();rep::AudioPlayer audition(std::move(solo));if(!audition.available())throw rep::Error(audition.error());audition.volume(0,true);audition.setRemainingPrefetchEnabled(false);
    auto soloSerial=audition.seek(0,true);ready(audition,soloSerial);auto duration=audition.durationMilliseconds();
    for(int n=0;n<2000&&audition.available()&&audition.positionMilliseconds()<duration+40;n++)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(!audition.available()||audition.positionMilliseconds()<duration+40)throw rep::Error("Audition did not reach its silent tail");
    audition.pauseBuffering(true);for(int n=0;n<1000&&!audition.stats().buffering;n++)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto auditionBuffers=submitted(soloIdentity);
    std::cout<<"{\"hiddenSerial\":"<<hiddenSerial<<",\"enabledSerial\":"<<enabledSerial<<",\"soloSerial\":"<<soloSerial<<",\"duration\":"<<duration<<',';
    print("hidden",hidden);std::cout<<',';print("enabled",enabled);std::cout<<',';print("audition",auditionBuffers);std::cout<<"}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what();return 1;}}
