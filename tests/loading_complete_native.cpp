#include "ui_playback.hpp"
#include "audio.hpp"
#include <atomic>
#include <iostream>
#include <thread>

namespace loading_probe {
std::atomic<unsigned> eagerPrograms=0,wholeDictionaryPrepares=0;
std::atomic<bool> picturePrepared=false,audioStartedBeforePicture=false;
std::atomic<bool> holdFirst=false,firstEntered=false,releaseFirst=false;
std::atomic<bool> skipPrefetch=false,holdDemand=false,demandEntered=false,releaseDemand=false;
std::atomic<rep::AudioPlayer*> liveAudio=nullptr;
std::atomic<bool> rejectAtFirstCommit=false;
std::unique_ptr<rep::Assets> sourceObserver;
void firstBoundary(){
    if(!holdFirst.load())return;firstEntered=true;
    while(!releaseFirst.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
class Gpu:public rep::Gpu {
public:
    using rep::Gpu::Gpu;
    size_t createAllPrograms(){eagerPrograms++;return rep::Gpu::createAllPrograms();}
};
class Executor:public rep::Executor {
public:
    using rep::Executor::Executor;
    void prepare(const std::function<bool()>& cancelled={}){wholeDictionaryPrepares++;rep::Executor::prepare(cancelled);}
};
class Assets:public rep::Assets {
public:
    using rep::Assets::Assets;
    void setDemandWaitCallback(std::function<void()> callback){
        rep::Assets::setDemandWaitCallback([callback=std::move(callback)]{
            callback();if(holdDemand&&picturePrepared){demandEntered=true;while(!releaseDemand.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        });
    }
    void prefetch(const std::vector<FrameRequest>& requests,uint64_t generation,const std::function<bool()>& cancelled={}){
        if(!skipPrefetch)rep::Assets::prefetch(requests,generation,cancelled);
    }
};
class Playback:public rep::Playback {
public:
    template<class... A> bool tick(A&&... args){firstBoundary();auto changed=rep::Playback::tick(std::forward<A>(args)...);if(changed)picturePrepared=true;return changed;}
    bool seekOrdinal(uint64_t ordinal,const std::function<bool()>& cancelled={},const std::function<void(uint64_t)>& progress={},bool force=false,const std::function<void(int64_t)>& preparing={},const std::function<bool()>& committing={}){
        firstBoundary();
        auto gate=[&]{auto ready=!committing||committing();if(ready&&rejectAtFirstCommit.exchange(false))sourceObserver->frame("sprite/test/frame.img",1,0,false);return ready;};
        auto changed=rep::Playback::seekOrdinal(ordinal,cancelled,progress,force,preparing,gate);
        if(changed)picturePrepared=true;return changed;
    }
};
class AudioPlayer:public rep::AudioPlayer {
public:
    explicit AudioPlayer(std::unique_ptr<rep::AudioTrack> track):rep::AudioPlayer(std::move(track)){liveAudio=this;}
    ~AudioPlayer(){liveAudio=nullptr;}
    uint64_t seek(int64_t milliseconds,bool playing,const std::function<bool()>& cancelled={}){
        if(playing&&!picturePrepared.load())audioStartedBeforePicture=true;
        return rep::AudioPlayer::seek(milliseconds,playing,cancelled);
    }
    bool resumePrepared(uint64_t serial){
        if(!picturePrepared.load())audioStartedBeforePicture=true;
        return rep::AudioPlayer::resumePrepared(serial);
    }
};
}
#define Gpu loading_probe::Gpu
#define Executor loading_probe::Executor
#define Playback loading_probe::Playback
#define AudioPlayer loading_probe::AudioPlayer
#define Assets loading_probe::Assets
#include "../src/ui_playback.cpp"
#undef AudioPlayer
#undef Assets
#undef Playback
#undef Executor
#undef Gpu

using namespace rep::ui;
template<class P> PlayerStatus until(PlaybackController& player,P predicate){
    auto deadline=Clock::now()+std::chrono::seconds(15);
    while(Clock::now()<deadline){
        MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        auto s=player.status();if(s.phase==Phase::Error)throw rep::Error(rep::utf8(s.message));
        if(predicate(s))return s;std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw rep::Error("loading regression timeout");
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=4)return 2;
    auto canvas=CreateWindowExW(0,L"STATIC",L"Owned loading gate regression",WS_OVERLAPPEDWINDOW,
        0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!canvas)throw rep::Error("cannot create regression canvas");
    bool noStalePicture=false,clockFrozen=false,noStaleTick=false,warmupUnpins=false,firstSourceRetry=false;
    {
        auto cache=std::make_shared<rep::RetainedCache>(1024*1024);
        rep::Replay input(argv[3]);rep::ui::FontWarmup warmer(cache);
        warmer.submit(input,argv[2],{3},1,[]{return false;});
        auto deadline=Clock::now()+std::chrono::seconds(5);
        while(cache->stats().entries<3&&Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(cache->stats().entries<3)throw rep::Error("font warmup fixture did not load glyphs");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));warmer.cancel();
        deadline=Clock::now()+std::chrono::milliseconds(500);
        while(cache->stats().activeBytes&&Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        warmupUnpins=cache->stats().activeBytes==0;
    }
    {
        PlaybackController player(canvas,argv[1],argv[2],true,Clock::now());
        player.setAudioMuted(true);player.open(argv[3]);
        until(player,[](const auto& s){return s.phase==Phase::Playing;});
        player.togglePause();until(player,[](const auto& s){return s.phase==Phase::Paused;});
    }
    auto eager=loading_probe::eagerPrograms.load(),prepares=loading_probe::wholeDictionaryPrepares.load();
    auto premature=loading_probe::audioStartedBeforePicture.load();
    loading_probe::picturePrepared=false;
    {
        PlaybackController player(canvas,argv[1],argv[2],true,Clock::now());player.setAudioMuted(true);
        auto deadline=Clock::now()+std::chrono::seconds(5);
        while(!player.imageIndex()){if(Clock::now()>deadline)throw rep::Error("initial index did not finish");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        loading_probe::sourceObserver=std::make_unique<rep::Assets>(player.imageIndex());loading_probe::rejectAtFirstCommit=true;
        player.open(argv[3]);auto ready=until(player,[](const auto& s){return s.phase==Phase::Playing;});
        firstSourceRetry=!loading_probe::rejectAtFirstCommit.load()&&loading_probe::sourceObserver->sourceRevision()>0&&ready.presentationSerial>0;
        loading_probe::sourceObserver.reset();
    }
    loading_probe::picturePrepared=false;loading_probe::skipPrefetch=true;loading_probe::holdDemand=true;
    {
        PlaybackController player(canvas,argv[1],argv[2],true,Clock::now());player.setAudioMuted(true);player.open(argv[3]);
        auto deadline=Clock::now()+std::chrono::seconds(15);
        while(!loading_probe::demandEntered.load()){
            if(Clock::now()>deadline)throw rep::Error("late demand boundary not reached");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        auto before=player.status();auto audio=loading_probe::liveAudio.load();
        auto position=audio->positionMilliseconds();std::this_thread::sleep_for(std::chrono::milliseconds(50));
        clockFrozen=audio->stats().buffering&&audio->positionMilliseconds()==position;
        player.seekTime(500);player.togglePause();loading_probe::releaseDemand=true;loading_probe::holdDemand=false;
        auto latest=until(player,[&](const auto& s){return s.phase==Phase::Paused&&s.elapsed==500&&s.completedNavigationSerial>before.completedNavigationSerial;});
        noStaleTick=latest.presentationSerial==before.presentationSerial+1;
    }
    loading_probe::skipPrefetch=false;
    loading_probe::holdFirst=true;loading_probe::firstEntered=false;loading_probe::releaseFirst=false;
    {
        PlaybackController player(canvas,argv[1],argv[2],true,Clock::now());
        player.setAudioMuted(true);player.open(argv[3]);
        auto deadline=Clock::now()+std::chrono::seconds(15);
        while(!loading_probe::firstEntered.load()){
            if(Clock::now()>deadline)throw rep::Error("first scene boundary not reached");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        auto serial=player.status().presentationSerial;player.stop();loading_probe::releaseFirst=true;
        auto stopped=until(player,[](const auto& s){return s.phase==Phase::Stopped;});
        noStalePicture=stopped.presentationSerial==serial;
    }
    DestroyWindow(canvas);
    std::cout<<"{\"no_eager_all_programs\":"<<(eager==0?"true":"false")
        <<",\"no_whole_dictionary_start_barrier\":"<<(prepares==0?"true":"false")
        <<",\"audio_never_starts_before_picture_prepared\":"<<(!premature?"true":"false")
        <<",\"stop_cancels_unpublished_first_picture\":"<<(noStalePicture?"true":"false")
        <<",\"cold_demand_confirms_frozen_audio_clock\":"<<(clockFrozen?"true":"false")
        <<",\"new_seek_cancels_unpublished_normal_tick\":"<<(noStaleTick?"true":"false")
        <<",\"cancelled_font_warmup_releases_active_pins\":"<<(warmupUnpins?"true":"false")
        <<",\"first_source_change_retries_outside_publication_lock\":"<<(firstSourceRetry?"true":"false")<<"}\n";
    return 0;
}catch(const std::exception& e){loading_probe::releaseFirst=true;loading_probe::releaseDemand=true;std::cerr<<e.what()<<'\n';return 1;}}
