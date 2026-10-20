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
    if(std::wstring_view(argv[1])==L"restart"){
        Player player(window,argv[2],argv[3],false,Clock::now());player.setAudioMuted(true);player.setPlaybackRate(2.);player.open(argv[4]);
        waitFor(player,[](const auto& s){return s.phase==Phase::Ended;});
        std::this_thread::sleep_for(std::chrono::milliseconds(180));
        auto restart=[&](bool space){
            if(space)player.togglePause();else player.replay();
            waitFor(player,[](const auto& s){return s.phase==Phase::Playing;});
            std::this_thread::sleep_for(std::chrono::milliseconds(50));auto pending=player.status();
            bool startsAtBeginning=pending.phase==Phase::Playing&&pending.elapsed<250;
            waitFor(player,[](const auto& s){return s.phase==Phase::Ended;});
            std::this_thread::sleep_for(std::chrono::milliseconds(180));return startsAtBeginning;
        };
        bool replay=restart(false),space=restart(true);
        std::cout<<"{\"implemented\":true,\"replay_from_beginning\":"<<replay<<",\"space_from_beginning\":"<<space<<"}";return;
    }
    if(std::wstring_view(argv[1])==L"speed"){
        if constexpr(!requires(Player& p){p.setPlaybackRate(2.);p.status().playbackRate;})std::cout<<"{\"implemented\":false}\n";
        else {
            Player player(window,argv[2],argv[3],false,Clock::now());player.setAudioMuted(true);player.setPlaybackRate(.5);player.open(argv[4]);
            auto first=waitFor(player,[](const auto& s){return s.phase==Phase::Playing&&s.frameCount&&s.audioAvailable&&s.elapsed>60;});
            std::this_thread::sleep_for(std::chrono::milliseconds(160));auto slow=player.status();auto slowDelta=slow.elapsed-first.elapsed;
            player.togglePause();auto paused=waitFor(player,[](const auto& s){return s.phase==Phase::Paused;});
            player.setPlaybackRate(2.);std::this_thread::sleep_for(std::chrono::milliseconds(80));auto changed=player.status();
            bool stable=changed.elapsed==paused.elapsed&&changed.ordinal==paused.ordinal&&changed.playbackRate==2.;
            player.togglePause();auto start=waitFor(player,[](const auto& s){return s.phase==Phase::Playing;});
            std::this_thread::sleep_for(std::chrono::milliseconds(160));auto fast=player.status();auto fastDelta=fast.elapsed-start.elapsed;
            bool sync=std::abs(fast.audioPosition-fast.elapsed)<45;
            player.togglePause();waitFor(player,[](const auto& s){return s.phase==Phase::Paused;});player.seekTime(300);
            auto seek=waitFor(player,[](const auto& s){return s.phase==Phase::Paused&&s.completedNavigationSerial==s.navigationSerial&&s.elapsed==300;});
            player.stepFrame(1);auto step=waitFor(player,[&](const auto& s){return s.phase==Phase::Paused&&s.ordinal>seek.ordinal;});
            player.replay();auto replay=waitFor(player,[](const auto& s){return s.phase==Phase::Playing&&s.elapsed<100;});
            std::cout<<"{\"implemented\":true,\"slow_delta\":"<<slowDelta<<",\"fast_delta\":"<<fastDelta<<",\"pause_stable\":"<<stable<<",\"sync\":"<<sync<<",\"fast_elapsed\":"<<fast.elapsed<<",\"fast_audio\":"<<fast.audioPosition<<",\"resume_elapsed\":"<<start.elapsed<<",\"seek_rate\":"<<seek.playbackRate<<",\"step_rate\":"<<step.playbackRate<<",\"replay_rate\":"<<replay.playbackRate<<"}";
        }
        return;
    }
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
        if(mode==L"canvas-active"){
            rep::CanvasSettings busy;busy.mode=rep::CanvasMode::Size;busy.width=busy.height=4096;
            auto before=Clock::now();player.setCanvasSettings(busy);
            auto resized=waitFor(player,[](const auto& s){return s.canvasWidth==4096;});
            auto rebuild=std::chrono::duration<double,std::milli>(Clock::now()-before).count();
            std::this_thread::sleep_for(std::chrono::milliseconds(60));resized=player.status();
            auto difference=std::abs(resized.audioPosition-resized.elapsed);
            std::cout<<"{\"implemented\":true,\"difference\":"<<difference<<",\"rebuild_ms\":"<<rebuild
                     <<",\"elapsed\":"<<resized.elapsed<<",\"audio_position\":"<<resized.audioPosition
                     <<",\"playing\":"<<(resized.phase==Phase::Playing?"true":"false")<<"}\n";return;
        }
        if(mode==L"error"){
            player.open(std::filesystem::path(argv[2])/L"broken.rep");
            auto deadline=Clock::now()+std::chrono::seconds(12);auto failed=player.status();
            while(Clock::now()<deadline&&failed.phase!=Phase::Error){std::this_thread::sleep_for(std::chrono::milliseconds(2));failed=player.status();}
            std::cout<<"{\"implemented\":true,\"error\":"<<(failed.phase==Phase::Error?"true":"false")
                     <<",\"available\":"<<(failed.audioAvailable?"true":"false")<<",\"events\":"<<failed.audioEvents
                     <<",\"position\":"<<failed.audioPosition<<"}\n";return;
        }
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
        player.togglePause();waitFor(player,[](const auto& s){return s.phase==Phase::Playing&&s.audioAvailable&&s.audioPosition>=25;});
        player.stop();waitFor(player,[](const auto& s){return s.phase==Phase::Stopped&&!s.audioAvailable;});
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
