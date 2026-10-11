#include "ui_playback.hpp"
#include "audio.hpp"
#include <iostream>
#include <thread>
#include <limits>

using namespace rep;
using namespace rep::ui;
template<class T> concept EngineNavigation = requires(T& p) {
    p.seek(11, false, std::function<bool()>{}, std::function<void(uint64_t)>{});
    p.seekOrdinal(uint64_t(2), std::function<bool()>{}, std::function<void(uint64_t)>{}, false);
};
template<class T> concept ControllerNavigation = requires(T& p) {
    p.beginScrub(); p.endScrub(std::optional<int64_t>(11), false);
    p.seekTime(11, false); p.stepFrame(1, true);
    p.status().completedNavigationSerial;
};
template<class T> auto waitFor(T& player, const auto& predicate) {
    auto deadline=Clock::now()+std::chrono::seconds(20);
    while(Clock::now()<deadline) {auto s=player.status();if(predicate(s))return s;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    throw Error("navigation wait timed out");
}
template<class P> void engineCase(const std::filesystem::path& root,const std::filesystem::path& client,const std::filesystem::path& path) {
    if constexpr(!EngineNavigation<P>)std::cout<<"{\"implemented\":false}\n";
    else {
        Gpu gpu(root/L"assets"/L"shaders");Assets assets(client/L"ImagePacks2");
        Replay replay(path);Executor executor(gpu,assets,root/L"runtime"/L"cache",client);executor.attach(replay);executor.prepare();
        P playback;playback.attach(replay,executor);playback.start();playback.pause();
        playback.seek(11,false,{},{});auto first=playback.ordinal();auto firstTime=playback.timestamp();
        auto before=executor.statistics.scenes;playback.seek(31,false,{},{});bool reused=executor.statistics.scenes-before==1;
        playback.seek(50,true,{},{});bool endpoint=playback.ordinal()==5&&playback.ended();
        int checks=0;bool cancelled=!playback.seekOrdinal(4,[&]{return ++checks>2;},{},true);
        before=executor.statistics.scenes;playback.seekOrdinal(4,{},{},false);bool rebuilt=executor.statistics.scenes-before==5;
        auto pixels=gpu.readback(executor.output());
        Replay reference(path);Executor expected(gpu,assets,root/L"runtime"/L"cache",client);expected.attach(reference);Scene scene;
        while(reference.next(scene)){expected.execute(scene);if(scene.ordinal==4)break;}
        bool accurate=pixels==gpu.readback(expected.output());
        std::cout<<"{\"implemented\":true,\"first\":"<<first<<",\"timestamp\":"<<firstTime
                 <<",\"reused\":"<<(reused?"true":"false")<<",\"endpoint\":"<<(endpoint?"true":"false")
                 <<",\"cancelled\":"<<(cancelled?"true":"false")<<",\"rebuilt_after_cancel\":"<<(rebuilt?"true":"false")
                 <<",\"accurate\":"<<(accurate?"true":"false")<<"}\n";
    }
}
template<class P> void controllerCase(HWND window,const std::filesystem::path& root,const std::filesystem::path& client,const std::filesystem::path& path,std::wstring_view mode) {
    if constexpr(!ControllerNavigation<P>)std::cout<<"{\"implemented\":false}\n";
    else {
        P player(window,root,client,true,Clock::now());player.setAudioMuted(true);player.open(path);
        auto opened=waitFor(player,[](const auto& s){return s.phase==Phase::Playing&&s.frameCount>0;});
        if(mode==L"scrub") {
            player.beginScrub();auto frozen=waitFor(player,[](const auto& s){return s.scrubbing;});
            std::this_thread::sleep_for(std::chrono::milliseconds(60));auto later=player.status();
            bool frozenClock=later.elapsed==frozen.elapsed;
            player.endScrub(11);auto applied=waitFor(player,[](const auto& s){return !s.scrubbing&&s.phase==Phase::Playing&&s.completedNavigationSerial>0;});
            bool synced=std::abs(applied.audioPosition-applied.elapsed)<=35;
            player.togglePause();waitFor(player,[](const auto& s){return s.phase==Phase::Paused;});
            player.seekTime(11);auto paused=waitFor(player,[&](const auto& s){return s.completedNavigationSerial>applied.completedNavigationSerial&&s.phase==Phase::Paused;});
            auto serial=paused.completedNavigationSerial;player.stepFrame(1);for(int n=0;n<40;n++)player.stepFrame(1,true);
            auto stepped=waitFor(player,[&](const auto& s){return s.completedNavigationSerial>serial&&s.phase==Phase::Paused;});
            std::this_thread::sleep_for(std::chrono::milliseconds(80));auto settled=player.status();
            bool repeatBounded=settled.ordinal<=paused.ordinal+2;
            player.seekTime(50,true);auto ended=waitFor(player,[&](const auto& s){return s.completedNavigationSerial>stepped.completedNavigationSerial&&s.phase==Phase::Ended;});
            std::cout<<"{\"implemented\":true,\"frozen\":"<<(frozenClock?"true":"false")<<",\"sync\":"<<(synced?"true":"false")
                     <<",\"paused_ordinal\":"<<paused.ordinal<<",\"repeat_bounded\":"<<(repeatBounded?"true":"false")
                     <<",\"endpoint_ordinal\":"<<ended.ordinal<<",\"audio_confirmed\":"<<(ended.audioPosition==ended.elapsed?"true":"false")<<"}\n";
        } else {
            player.togglePause();auto paused=waitFor(player,[](const auto& s){return s.phase==Phase::Paused;});
            player.seekTime(900);auto seeking=waitFor(player,[](const auto& s){return s.phase==decltype(s.phase)::Seeking;});
            for(int n=0;n<30;n++)player.stepFrame(1,true);
            player.seekTime(20);auto applied=waitFor(player,[&](const auto& s){return s.phase==Phase::Paused&&s.completedNavigationSerial>seeking.navigationSerial;});
            bool latest=applied.elapsed==20;
            player.seekTime(1000);waitFor(player,[](const auto& s){return s.phase==decltype(s.phase)::Seeking;});player.stop();
            auto stopped=waitFor(player,[](const auto& s){return s.phase==Phase::Stopped&&!s.audioAvailable;});
            std::this_thread::sleep_for(std::chrono::milliseconds(80));bool noStale=player.status().phase==Phase::Stopped;
            std::cout<<"{\"implemented\":true,\"latest\":"<<(latest?"true":"false")<<",\"stop_wins\":"<<(noStale?"true":"false")<<"}\n";
        }
    }
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=5)return 2;HWND window=nullptr;
    try {
        std::wstring mode=argv[1];if(mode==L"engine"){engineCase<Playback>(argv[2],argv[3],argv[4]);return 0;}
        WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"NavigationContract";RegisterClassW(&cls);
        window=CreateWindowW(cls.lpszClassName,L"Owned navigation test",WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,cls.hInstance,nullptr);
        controllerCase<PlaybackController>(window,argv[2],argv[3],argv[4],mode);DestroyWindow(window);return 0;
    }catch(const std::exception& e){if(window)DestroyWindow(window);std::cerr<<e.what()<<'\n';return 1;}
}
