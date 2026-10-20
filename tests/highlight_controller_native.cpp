#include "ui_playback.hpp"
#include <iostream>
#include <thread>
using namespace rep::ui;
template<class P,class Predicate> auto settle(P& player,Predicate predicate){
    auto deadline=Clock::now()+std::chrono::seconds(12);
    while(Clock::now()<deadline){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}auto s=player.status();if(s.phase==Phase::Error)throw rep::Error(rep::utf8(s.message));if(predicate(s))return s;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    throw rep::Error("highlight controller timed out");
}
template<class P> int verify(P& player,const wchar_t* path){
    if constexpr(requires{player.setHighlightedImage(std::string{});}){
        player.open(path);settle(player,[](auto s){return s.phase==Phase::Ended;});player.captureFrame();auto original=settle(player,[](auto s){return s.captureSerial>0;});
        player.setHighlightedImage("SPRITE\\TEST\\FRAME.IMG");auto selected=settle(player,[&](auto s){return s.highlightAvailable&&s.presentationSerial>original.presentationSerial;});
        bool ok=selected.phase==Phase::Ended&&selected.ordinal==original.ordinal&&selected.elapsed==original.elapsed&&selected.highlightedImage=="sprite/test/frame.img";
        player.captureFrame();auto captured=settle(player,[&](auto s){return s.captureSerial>original.captureSerial;});ok&=captured.frameCrc==original.frameCrc;
        player.setHighlightedImage("");auto cleared=settle(player,[&](auto s){return !s.highlightAvailable&&s.presentationSerial>selected.presentationSerial;});ok&=cleared.phase==Phase::Ended&&cleared.highlightedImage.empty();
        player.setHighlightedImage("sprite/test/frame.img");settle(player,[](auto s){return s.highlightAvailable;});player.stop();auto stopped=settle(player,[](auto s){return s.phase==Phase::Stopped;});player.setHighlightedImage("");auto stoppedClear=settle(player,[&](auto s){return !s.highlightAvailable&&s.presentationSerial>stopped.presentationSerial;});ok&=stoppedClear.phase==Phase::Stopped;
        player.setHighlightedImage("sprite/test/frame.img");settle(player,[](auto s){return s.highlightAvailable;});player.open(path);auto opened=settle(player,[](auto s){return s.phase==Phase::Ended&&s.highlightedImage.empty();});ok&=!opened.highlightAvailable;
        player.setHighlightedImage("sprite/test/frame.img");auto held=settle(player,[](auto s){return s.highlightAvailable;});player.replay();
        auto replayed=settle(player,[&](auto s){return s.phase==Phase::Ended&&s.presentationSerial>held.presentationSerial;});ok&=replayed.highlightAvailable&&replayed.highlightedImage=="sprite/test/frame.img";
        player.seekTime(10);auto paused=settle(player,[](auto s){return s.phase==Phase::Paused&&s.ordinal==1;});ok&=paused.highlightAvailable;
        player.stepFrame(1);auto stepped=settle(player,[](auto s){return s.phase==Phase::Paused&&s.ordinal==2;});ok&=stepped.highlightAvailable;
        std::cout<<(ok?"PASS":"FAIL")<<" highlight follows replay, paused seek and recorded-frame stepping\n";
        std::cout<<(ok?"PASS":"FAIL")<<" ended/stopped selection, clear, original CRC, canonical path and REP replacement\n";
        player.configureClient(opened.client);settle(player,[](auto s){return s.phase==Phase::Empty;});Sleep(100);
        auto cpu=[](){FILETIME created{},exited{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user);ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;return k.QuadPart+u.QuadPart;};
        auto before=cpu();player.setHighlightedImage({});Sleep(250);auto used=(cpu()-before)/10000;
        bool idle=used<100;std::cout<<(idle?"PASS":"FAIL")<<" empty controller sleeps after clearing highlight (CPU "<<used<<" ms)\n";
        return ok&&idle?0:1;
    }else{std::cout<<"FAIL preview controller highlight command is missing\n";return 1;}
}
int wmain(int argc,wchar_t** argv){if(argc!=4)return 2;try{
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"ImageHighlightProbe";RegisterClassW(&wc);
    HWND first=CreateWindowW(wc.lpszClassName,L"Highlight probe",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,wc.hInstance,nullptr);
    int result;{
#ifdef PRISM_HIGHLIGHT
        HWND second=CreateWindowW(wc.lpszClassName,L"Original highlight probe",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,wc.hInstance,nullptr);
        PlaybackController player(first,argv[1],argv[2],false,Clock::now(),{}, {},true,second);
#else
        PlaybackController player(first,argv[1],argv[2],false,Clock::now());
#endif
        result=verify(player,argv[3]);
    }DestroyWindow(first);return result;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
