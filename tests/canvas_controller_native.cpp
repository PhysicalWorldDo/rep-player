#include "../src/ui_playback.hpp"
#include <windows.h>
#include <chrono>
#include <iostream>
#include <thread>

using namespace rep::ui;

template<class Predicate>
PlayerStatus waitFor(PlaybackController& player,Predicate predicate){
    auto deadline=Clock::now()+std::chrono::seconds(12);
    PlayerStatus status;
    while(Clock::now()<deadline){
        MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        status=player.status();
        if(status.phase==Phase::Error)throw rep::Error(rep::utf8(status.message));
        if(predicate(status))return status;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw rep::Error("Controller probe timed out");
}

int wmain(int argc,wchar_t** argv){
    if(argc!=4)return 2;
    HWND window=nullptr;
    try{
        WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"CanvasControllerProbe";RegisterClassW(&type);
        window=CreateWindowW(type.lpszClassName,L"Canvas controller regression",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,type.hInstance,nullptr);
        if(!window)throw rep::Error("Cannot create owned probe window");
        ShowWindow(window,SW_SHOWNOACTIVATE);
        bool pass=true;int failedWidth=0,failedHeight=0,trials=0;
        {
            PlaybackController player(window,argv[1],argv[2],false,Clock::now());
            player.open(argv[3]);waitFor(player,[](const auto& status){return status.phase==Phase::Ended;});
            rep::CanvasSettings busy;busy.mode=rep::CanvasMode::Size;busy.width=busy.height=4096;
            rep::CanvasSettings requested;requested.mode=rep::CanvasMode::Multiple;requested.factor=2;
            for(int trial=0;trial<4;trial++){
                player.setCanvasSettings(busy);
                auto before=waitFor(player,[](const auto& status){return status.canvasWidth==4096&&status.canvasHeight==4096;});
                // An actual GPU readback gives the worker pending work while
                // Apply and Stop are queued through the public controller API.
                player.captureFrame();std::this_thread::sleep_for(std::chrono::milliseconds(1));
                player.setCanvasSettings(requested);player.stop();
                waitFor(player,[](const auto& status){return status.phase==Phase::Stopped;});
                auto settled=waitFor(player,[&](const auto& status){return status.captureSerial>before.captureSerial;});
                std::this_thread::sleep_for(std::chrono::milliseconds(80));settled=player.status();
                trials++;
                if(settled.phase!=Phase::Stopped||settled.canvasWidth!=32||settled.canvasHeight!=32){pass=false;failedWidth=settled.canvasWidth;failedHeight=settled.canvasHeight;break;}
            }
        }
        DestroyWindow(window);window=nullptr;
        std::cout<<"{\"pass\":"<<(pass?"true":"false")<<",\"trials\":"<<trials<<",\"expected_width\":32,\"expected_height\":32,\"failed_width\":"<<failedWidth<<",\"failed_height\":"<<failedHeight<<"}\n";
        return pass?0:1;
    }catch(const std::exception& error){if(window)DestroyWindow(window);std::cerr<<error.what()<<'\n';return 2;}
}
