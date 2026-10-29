#include "ui_playback.hpp"
#include "audio.hpp"
#include <atomic>
#include <iostream>
#include <thread>

namespace measured {
std::atomic<bool> watching=false,audioBeforeSelection=false;
std::atomic<bool> blockAudio=false,audioBlocked=false;
std::atomic<int64_t> audioTarget=-1;
std::atomic<size_t> selections=0,refreshes=0;
std::atomic<int> publicationPhase=0;
std::atomic<bool> publicationEntered=false,publicationRelease=false;
void publicationBoundary(int phase){
    if(publicationPhase.load()!=phase)return;publicationEntered=true;
    while(!publicationRelease.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
class AudioPlayer:public rep::AudioPlayer {
    template<class Base> static uint64_t submit(Base& base,int64_t target,bool playing,const std::function<bool()>& cancelled){
        if constexpr(requires {base.seek(target,playing,cancelled);})return base.seek(target,playing,cancelled);
        else return base.seek(target,playing);
    }
public:
    using rep::AudioPlayer::AudioPlayer;
    uint64_t seek(int64_t target,bool playing,const std::function<bool()>& cancelled={}){
        if(watching)audioTarget=target;
        if(blockAudio&&target==800)return submit(static_cast<rep::AudioPlayer&>(*this),target,playing,[cancelled]{
            audioBlocked=true;
            while(blockAudio){if(cancelled&&cancelled())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            return cancelled&&cancelled();
        });
        return submit(static_cast<rep::AudioPlayer&>(*this),target,playing,cancelled);
    }
};
class Playback:public rep::Playback {
public:
    template<class... Args> bool seek(int64_t target,Args&&... args){
        if(watching){selections++;audioBeforeSelection=audioTarget.load()==target;}
        auto changed=rep::Playback::seek(target,std::forward<Args>(args)...);publicationBoundary(1);return changed;
    }
    template<class... Args> bool seekOrdinal(uint64_t ordinal,Args&&... args){
        if(watching)selections++;
        return rep::Playback::seekOrdinal(ordinal,std::forward<Args>(args)...);
    }
    template<class... Args> bool refresh(Args&&... args){
        if(watching)refreshes++;
        return rep::Playback::refresh(std::forward<Args>(args)...);
    }
};
}
// Both wrappers delegate to the actual engine and XAudio2 implementation.
// They only observe calls crossing the controller's two component boundaries.
#define Playback measured::Playback
#define AudioPlayer measured::AudioPlayer
#include "../src/ui_playback.cpp"
#undef AudioPlayer
#undef Playback

using namespace rep::ui;
template<class Predicate> static PlayerStatus waitFor(PlaybackController& player,Predicate predicate){
    auto end=Clock::now()+std::chrono::seconds(15);
    while(Clock::now()<end){
        MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        auto s=player.status();if(s.phase==Phase::Error)throw rep::Error(rep::utf8(s.message));
        if(predicate(s))return s;std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw rep::Error("controller navigation wait timed out");
}
static bool publicationCase(HWND window,wchar_t** argv,int phase,bool scrub){
    PlaybackController player(window,argv[1],argv[2],true,Clock::now());player.setAudioMuted(true);player.open(argv[3]);
    waitFor(player,[](auto& s){return s.phase==Phase::Playing&&s.frameCount&&s.audioAvailable;});
    player.togglePause();auto paused=waitFor(player,[](auto& s){return s.phase==Phase::Paused;});
    player.seekTime(0);auto before=waitFor(player,[&](auto& s){return s.phase==Phase::Paused&&s.elapsed==0&&s.completedNavigationSerial>paused.completedNavigationSerial;});
    measured::publicationEntered=false;measured::publicationRelease=false;measured::publicationPhase=phase;
    player.seekTime(500);auto deadline=Clock::now()+std::chrono::seconds(5);
    while(!measured::publicationEntered.load()){
        if(Clock::now()>=deadline){measured::publicationRelease=true;throw rep::Error("publication boundary was not reached");}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::atomic<bool> actionStarted=false,actionReturned=false;PlayerStatus observed;
    std::thread action([&]{actionStarted=true;if(scrub)player.beginScrub();else player.stop();observed=player.status();actionReturned=true;});
    while(!actionStarted.load())std::this_thread::yield();
    deadline=Clock::now()+std::chrono::milliseconds(100);
    while(!actionReturned.load()&&Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    bool protectedUntilPublication=!actionReturned.load();measured::publicationRelease=true;action.join();measured::publicationPhase=0;
    bool consistent=observed.ordinal==2&&observed.elapsed==500&&observed.completedNavigationSerial>before.completedNavigationSerial&&observed.frameCrc!=before.frameCrc;
    return protectedUntilPublication&&consistent;
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=4)return 2;
    auto window=CreateWindowExW(0,L"STATIC",L"Owned native seek controller regression",WS_OVERLAPPEDWINDOW,0,0,160,160,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)throw rep::Error("cannot create owned regression canvas");
    {
        PlaybackController player(window,argv[1],argv[2],true,Clock::now());player.setAudioMuted(true);player.open(argv[3]);
        waitFor(player,[](auto& s){return s.phase==Phase::Playing&&s.frameCount&&s.audioAvailable;});
        player.togglePause();auto paused=waitFor(player,[](auto& s){return s.phase==Phase::Paused;});
        measured::watching=true;player.seekTime(500);
        auto sought=waitFor(player,[&](auto& s){return s.phase==Phase::Paused&&s.completedNavigationSerial>paused.completedNavigationSerial&&s.elapsed==500;});
        auto selected=measured::selections.load();auto beforeRefresh=measured::refreshes.load();auto crc=sought.frameCrc;
        player.setNegativeImgOffsetsEnabled(true);
        auto redraw=waitFor(player,[&](auto& s){return s.phase==Phase::Paused&&s.completedNavigationSerial>sought.completedNavigationSerial;});
        bool refreshed=measured::selections.load()==selected&&measured::refreshes.load()>beforeRefresh;
        bool stable=redraw.frameCrc==crc&&redraw.ordinal==sought.ordinal&&redraw.elapsed==sought.elapsed;
        auto checkRefresh=[&](const auto& change){
            auto before=player.status();auto count=measured::selections.load();auto refresh=measured::refreshes.load();change();
            auto after=waitFor(player,[&](auto& s){return s.phase==Phase::Paused&&s.completedNavigationSerial>before.completedNavigationSerial;});
            refreshed&=measured::selections.load()==count&&measured::refreshes.load()>refresh;
            stable&=after.ordinal==before.ordinal&&after.elapsed==before.elapsed;return after;
        };
        auto hidden=checkRefresh([&]{player.setHiddenImages({"sprite/test/frame.img"});});
        auto shown=checkRefresh([&]{player.setHiddenImages({});});
        bool visibility=hidden.frameCrc!=crc&&shown.frameCrc==crc;
        auto resized=checkRefresh([&]{rep::CanvasSettings settings;settings.factor=2;player.setCanvasSettings(settings);});
        bool canvas=resized.canvasWidth==32&&resized.canvasHeight==32;
        auto beforeCancel=measured::selections.load();player.beginScrub();
        waitFor(player,[](auto& s){return s.scrubbing;});player.endScrub();
        auto cancelled=waitFor(player,[&](auto& s){return s.phase==Phase::Paused&&!s.scrubbing&&s.completedNavigationSerial>resized.completedNavigationSerial;});
        bool cancelStable=measured::selections.load()==beforeCancel&&cancelled.ordinal==resized.ordinal&&cancelled.frameCrc==resized.frameCrc;
        measured::blockAudio=true;player.seekTime(800);
        auto blocked=waitFor(player,[](auto& s){return s.phase==Phase::Seeking&&measured::audioBlocked.load();});
        bool noStale=blocked.completedNavigationSerial==cancelled.completedNavigationSerial&&blocked.frameCrc==cancelled.frameCrc;
        player.seekTime(300);
        auto latest=waitFor(player,[&](auto& s){return s.phase==Phase::Paused&&s.elapsed==300&&s.completedNavigationSerial>blocked.navigationSerial;});
        measured::blockAudio=false;std::this_thread::sleep_for(std::chrono::milliseconds(30));auto settled=player.status();
        bool superseded=noStale&&settled.completedNavigationSerial==latest.completedNavigationSerial&&settled.elapsed==300&&settled.audioPosition==300;
        measured::watching=false;
        bool stopAfterInstallation=publicationCase(window,argv,1,false);
        bool stopBeforeStatus=publicationCase(window,argv,2,false);
        bool scrubBeforeStatus=publicationCase(window,argv,2,true);
        std::cout<<"{\"audio_before_selection\":"<<(measured::audioBeforeSelection?"true":"false")
                 <<",\"redraw_without_reselection\":"<<(refreshed?"true":"false")
                 <<",\"redraw_stable\":"<<(stable?"true":"false")
                 <<",\"audio_aligned\":"<<(redraw.audioPosition==redraw.elapsed?"true":"false")
                 <<",\"visibility\":"<<(visibility?"true":"false")<<",\"canvas\":"<<(canvas?"true":"false")
                 <<",\"cancel_stable\":"<<(cancelStable?"true":"false")<<",\"superseded_before_commit\":"<<(superseded?"true":"false")
                 <<",\"stop_after_installation_atomic\":"<<(stopAfterInstallation?"true":"false")
                 <<",\"stop_before_status_atomic\":"<<(stopBeforeStatus?"true":"false")
                 <<",\"scrub_before_status_atomic\":"<<(scrubBeforeStatus?"true":"false")<<"}\n";
    }
    DestroyWindow(window);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
