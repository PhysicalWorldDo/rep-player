#include "ui_playback.hpp"
#include <iostream>
#include <thread>
#ifdef OGG_CONTROLLER_GATE
std::atomic<bool> oggGateRequested=false,oggGateEntered=false,oggGateReleased=false;
void oggSnapshotBoundary(){
    if(!oggGateRequested.exchange(false))return;
    oggGateEntered=true;while(!oggGateReleased)std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
#endif

using namespace rep::ui;
template<class Predicate> PlayerStatus waitFor(PlaybackController& player,Predicate predicate){
    auto deadline=Clock::now()+std::chrono::seconds(15);
    while(Clock::now()<deadline){
        MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        auto s=player.status();if(s.phase==Phase::Error)throw rep::Error(rep::utf8(s.message));
        if(predicate(s))return s;std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw rep::Error("OGG controller state did not complete");
}
static void require(bool value,const char* message){if(!value)throw rep::Error(message);}
template<class Controller> void exercise(HWND window,int argc,wchar_t** argv){
    if constexpr(!requires(Controller& c,std::unordered_set<std::string> paths){
        c.setHiddenOggs(paths);c.auditionOgg(std::string{});
        c.status().allOggs;c.status().currentOggs;c.status().hiddenOggs;c.status().auditionPath;
    }){std::cout<<"{\"implemented\":false}\n";}
    else {
        if(argc!=5)throw rep::Error("Owned root, client, replay and other replay required");
        Controller player(window,argv[1],argv[2],false,Clock::now());
        player.setAudioMuted(true);player.setAudioVolume(.35f);player.open(argv[3]);
        waitFor(player,[](const auto& s){return s.phase==Phase::Playing&&s.frameCount>0;});
        player.togglePause();waitFor(player,[](const auto& s){return s.phase==Phase::Paused;});
        player.seekTime(0);auto first=waitFor(player,[](const auto& s){return s.phase==Phase::Paused&&s.ordinal==0;});
        const std::string a="sounds/effect/fixture/tone_a.ogg",b="sounds/effect/fixture/tone_b.ogg";
        require(first.allOggs==std::vector<std::string>({a,b}),"Whole REP OGG list differs");
        require(first.currentOggs==std::vector<std::string>({a,b}),"Current OGG list must include both voices alive at timestamp zero");
        player.stepFrame(1);auto second=waitFor(player,[](const auto& s){return s.phase==Phase::Paused&&s.ordinal==1;});
        require(second.currentOggs==std::vector<std::string>({a,b}),"Same timestamp must have the same active voices across record ordinals");
        player.seekTime(100);auto sustained=waitFor(player,[](const auto& s){return s.phase==Phase::Paused&&s.elapsed==100;});
        require(sustained.currentOggs==std::vector<std::string>({a,b}),"OGG vanished on a record with no new sound request");
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        require(player.status().currentOggs==sustained.currentOggs,"Paused activity changed with wall time");
        player.seekTime(400);auto expired=waitFor(player,[](const auto& s){return s.phase==Phase::Paused&&s.elapsed==400;});
        require(expired.currentOggs==std::vector<std::string>({a}),"Finished OGG must disappear while continuous loop remains");
        player.seekTime(500);auto retriggered=waitFor(player,[](const auto& s){return s.phase==Phase::Paused&&s.elapsed==500;});
        require(retriggered.currentOggs==std::vector<std::string>({a,b}),"Retriggered OGG did not reappear");
        second=retriggered;
        player.setHiddenOggs({a});auto muted=waitFor(player,[&](const auto& s){return s.phase==Phase::Paused&&s.hiddenOggs.contains(a)&&s.completedNavigationSerial==s.navigationSerial;});
        require(muted.ordinal==second.ordinal&&muted.elapsed==second.elapsed,"Paused mute moved REP");
        require(muted.currentOggs==std::vector<std::string>({a,b}),"Muted OGG must remain available for re-enabling");
        player.auditionOgg(a);auto audition=waitFor(player,[&](const auto& s){return s.auditionPath==a;});
        require(audition.phase==Phase::Paused&&audition.elapsed==muted.elapsed&&audition.hiddenOggs.contains(a),"Audition changed position or enabled hidden OGG");
        auto finished=waitFor(player,[](const auto& s){return s.auditionPath.empty();});
        require(finished.phase==Phase::Paused&&finished.elapsed==muted.elapsed,"Audition must finish paused at same position");
        player.auditionOgg(a);waitFor(player,[&](const auto& s){return s.auditionPath==a;});
        player.auditionOgg(a);waitFor(player,[](const auto& s){return s.auditionPath.empty();});
        player.auditionOgg(a);waitFor(player,[&](const auto& s){return s.auditionPath==a;});
        player.auditionOgg(b);waitFor(player,[&](const auto& s){return s.auditionPath==b;});
        player.togglePause();waitFor(player,[](const auto& s){return s.phase==Phase::Playing&&s.auditionPath.empty()&&s.elapsed>=100;});
#ifdef OGG_CONTROLLER_GATE
        oggGateRequested=true;
        for(int n=0;n<5000&&!oggGateEntered;n++)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(oggGateEntered,"Controller snapshot gate not reached");
        player.auditionOgg(a);player.setHiddenOggs({a,b});oggGateReleased=true;
        auto combined=waitFor(player,[&](const auto& s){return s.auditionPath==a&&s.completedNavigationSerial==s.navigationSerial;});
        require(combined.phase==Phase::Paused,"Mute refresh resumed REP over audition");
        player.auditionOgg(a);waitFor(player,[](const auto& s){return s.auditionPath.empty();});
        player.togglePause();waitFor(player,[](const auto& s){return s.phase==Phase::Playing;});
#endif
        player.setHiddenOggs({b});auto playing=waitFor(player,[&](const auto& s){return s.phase==Phase::Playing&&s.hiddenOggs.contains(b)&&!s.hiddenOggs.contains(a)&&s.completedNavigationSerial==s.navigationSerial;});
        require(playing.elapsed>=100,"Playing mute restarted REP");
        player.setHiddenOggs({a});player.setHiddenOggs({});player.setHiddenOggs({b});
        waitFor(player,[&](const auto& s){return s.phase==Phase::Playing&&s.hiddenOggs==std::unordered_set<std::string>({b})&&s.completedNavigationSerial==s.navigationSerial;});
        player.replay();auto restarted=waitFor(player,[](const auto& s){return s.phase==Phase::Playing&&s.elapsed<100&&s.frameCount>0;});
        require(restarted.hiddenOggs.contains(b),"Replay lost OGG choices");
        player.refreshImages();auto refreshed=waitFor(player,[](const auto& s){return s.phase==Phase::Playing&&s.frameCount>0;});
        require(refreshed.hiddenOggs.contains(b),"NPK refresh lost OGG choices");
        player.open(argv[4]);auto changed=waitFor(player,[](const auto& s){return s.phase==Phase::Playing&&s.frameCount>0&&s.allOggs.size()==1;});
        require(changed.hiddenOggs.empty()&&changed.allOggs==std::vector<std::string>({b}),"New REP did not reset OGG choices");
        player.seekTime(200);waitFor(player,[](const auto& s){return s.phase==Phase::Playing&&s.elapsed>=200;});
        player.auditionOgg(b);waitFor(player,[&](const auto& s){return s.auditionPath==b;});
        player.stop();auto stopped=waitFor(player,[](const auto& s){return s.phase==Phase::Stopped&&s.auditionPath.empty();});
        require(stopped.currentOggs.empty(),"Stopped replay retained active OGGs");
        player.auditionOgg(b);waitFor(player,[&](const auto& s){return s.auditionPath==b;});
        auto stoppedPreview=player.status();
        rep::CanvasSettings larger;larger.mode=rep::CanvasMode::Multiple;larger.factor=2;player.setCanvasSettings(larger);
        auto redrawn=waitFor(player,[](const auto& s){return s.phase==Phase::Paused&&s.canvasWidth==32;});
        require(redrawn.elapsed==stoppedPreview.elapsed&&redrawn.audioPosition==redrawn.elapsed&&redrawn.currentOggs==std::vector<std::string>({b}),"Paused redraw after Stop/audition must restore preserved audio target");
        player.seekTime(0);auto restored=waitFor(player,[](const auto& s){return s.phase==Phase::Paused&&s.auditionPath.empty();});
        require(restored.currentOggs==std::vector<std::string>({b}),"Seek after Stop must restore target audio activity");
        player.auditionOgg("sounds/not-recorded.ogg");auto rejected=waitFor(player,[](const auto& s){return !s.auditionMessage.empty();});
        require(rejected.phase==Phase::Paused&&rejected.auditionPath.empty(),"Failed audition disturbed playback");
        player.togglePause();auto ended=waitFor(player,[](const auto& s){return s.phase==Phase::Ended;});
        require(ended.currentOggs.empty(),"Ended replay retained active OGGs");
        std::cout<<"{\"implemented\":true,\"ordinal_lists\":true,\"paused_mute\":true,\"playing_mute\":true,\"audition_once\":true,\"disabled_audition\":true,\"audition_replace_stop\":true,\"replay_refresh_choices\":true,\"switch_reset\":true,\"invalid_audition\":true}\n";
    }
}
int wmain(int argc,wchar_t** argv){HWND window=nullptr;try{
    WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"OggControllerProbe";RegisterClassW(&type);
    window=CreateWindowW(type.lpszClassName,L"Owned OGG test",WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,type.hInstance,nullptr);
    if(!window)throw rep::Error("Cannot create owned OGG test window");
    exercise<PlaybackController>(window,argc,argv);DestroyWindow(window);return 0;
}catch(const std::exception& e){if(window)DestroyWindow(window);std::cerr<<e.what()<<'\n';return 1;}}
