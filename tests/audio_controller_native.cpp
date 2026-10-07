#include "../src/ui_playback.hpp"
#include <windows.h>
#include <chrono>
#include <iostream>
#include <thread>

using namespace rep::ui;
template<class Player,class Predicate>
auto waitFor(Player& player,Predicate predicate){
    auto deadline=Clock::now()+std::chrono::seconds(12);auto status=player.status();
    while(Clock::now()<deadline){
        MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        status=player.status();if(status.phase==Phase::Error)throw rep::Error(rep::utf8(status.message));
        if(predicate(status))return status;std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw rep::Error("Audio controller timed out");
}
template<class Player>
void exercise(HWND window,int argc,wchar_t** argv){
    if constexpr(!requires(Player& p){p.setAudioVolume(.25f);p.setAudioMuted(true);p.status().audioAvailable;p.status().audioPosition;p.status().audioEvents;}){
        std::cout<<"{\"implemented\":false}\n";
    }else{
        Player player(window,argv[2],argv[3],false,Clock::now());player.setAudioMuted(true);player.setAudioVolume(.35f);
        std::wstring mode=argv[1];
        if(mode==L"silent"||mode==L"missing"){
            player.configureClient(argv[5]);player.open(argv[4]);
            auto result=waitFor(player,[&](const auto& s){return s.phase==Phase::Playing&&(mode==L"silent"||s.missingSoundCount>0);});
            std::cout<<"{\"implemented\":true,\"available\":"<<(result.audioAvailable?"true":"false")
                     <<",\"events\":"<<result.audioEvents<<",\"missing\":"<<result.missingSoundCount
                     <<",\"message_empty\":"<<(result.audioMessage.empty()?"true":"false")
                     <<",\"playing\":"<<(result.phase==Phase::Playing?"true":"false")<<"}\n";return;
        }
        player.open(argv[4]);
        auto opened=waitFor(player,[](const auto& s){return s.phase==Phase::Playing&&s.audioAvailable&&s.audioPosition>=80;});
        if(mode==L"end"){
            auto ended=waitFor(player,[](const auto& s){return s.phase==Phase::Ended;});
            std::this_thread::sleep_for(std::chrono::milliseconds(80));auto later=player.status();
            std::cout<<"{\"implemented\":true,\"ended\":"<<(later.phase==Phase::Ended?"true":"false")
                     <<",\"position\":"<<later.audioPosition<<",\"duration\":"<<later.duration
                     <<",\"stable\":"<<(later.audioPosition==ended.audioPosition?"true":"false")<<"}\n";return;
        }
        player.togglePause();auto paused=waitFor(player,[](const auto& s){return s.phase==Phase::Paused;});
        std::this_thread::sleep_for(std::chrono::milliseconds(30));auto frozen=player.status();
        std::this_thread::sleep_for(std::chrono::milliseconds(70));auto still=player.status();
        bool pauseStable=std::abs(still.audioPosition-frozen.audioPosition)<=1;
        rep::CanvasSettings canvas;canvas.mode=rep::CanvasMode::Multiple;canvas.factor=2;player.setCanvasSettings(canvas);
        player.setHiddenImages({"sprite/test/frame.img"});
        auto refreshed=waitFor(player,[](const auto& s){return s.canvasWidth==32&&s.hiddenImages.contains("sprite/test/frame.img");});
        std::this_thread::sleep_for(std::chrono::milliseconds(30));refreshed=player.status();
        bool refreshStable=std::abs(refreshed.audioPosition-frozen.audioPosition)<=1&&refreshed.audioEvents==2;
        player.stepFrame(-1);auto stepped=waitFor(player,[&](const auto& s){return s.phase==Phase::Paused&&s.ordinal<paused.ordinal&&std::abs(s.audioPosition-s.elapsed)<=12;});
        player.togglePause();auto resumed=waitFor(player,[&](const auto& s){return s.phase==Phase::Playing&&s.audioPosition>=stepped.audioPosition+25;});
        bool sync=std::abs(resumed.audioPosition-resumed.elapsed)<=35;
        player.replay();auto restarted=waitFor(player,[&](const auto& s){return s.phase==Phase::Playing&&s.audioAvailable&&s.ordinal<=1&&s.audioEvents==2;});
        bool settings=restarted.audioMuted&&std::abs(restarted.audioVolume-.35f)<.0001f;
        player.stop();auto stopped=waitFor(player,[](const auto& s){return s.phase==Phase::Stopped&&!s.audioAvailable&&s.audioPosition==0;});
        player.configureClient(argv[5]);player.open(argv[6]);
        auto silent=waitFor(player,[](const auto& s){return s.phase==Phase::Playing;});
        std::cout<<"{\"implemented\":true,\"pause_stable\":"<<(pauseStable?"true":"false")
                 <<",\"refresh_stable\":"<<(refreshStable?"true":"false")<<",\"sync\":"<<(sync?"true":"false")
                 <<",\"audio_position\":"<<resumed.audioPosition<<",\"elapsed\":"<<resumed.elapsed
                 <<",\"step_position\":"<<stepped.audioPosition<<",\"step_elapsed\":"<<stepped.elapsed
                 <<",\"settings_survive\":"<<(settings&&silent.audioMuted&&std::abs(silent.audioVolume-.35f)<.0001f?"true":"false")
                 <<",\"switched_available\":"<<(silent.audioAvailable?"true":"false")
                 <<",\"switched_events\":"<<silent.audioEvents<<",\"switched_missing\":"<<silent.missingSoundCount<<"}\n";
    }
}
int wmain(int argc,wchar_t** argv){
    if(argc!=7)return 2;HWND window=nullptr;
    try{
        WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"AudioControllerProbe";RegisterClassW(&type);
        window=CreateWindowW(type.lpszClassName,L"Muted audio controller regression",WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,type.hInstance,nullptr);
        if(!window)throw rep::Error("Cannot create owned audio probe window");
        exercise<PlaybackController>(window,argc,argv);DestroyWindow(window);return 0;
    }catch(const std::exception& error){if(window)DestroyWindow(window);std::cerr<<error.what()<<'\n';return 1;}
}
