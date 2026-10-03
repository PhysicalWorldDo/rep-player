#include "ui_playback.hpp"
#include <psapi.h>
#include <iostream>
#include <stdexcept>

template<class T> static void clearDecoded(T& assets) {
    // The baseline has no release operation. Keeping this harness compatible
    // lets the ownership assertion fail at runtime before the API is added.
    if constexpr(requires { assets.clearDecodedImages(); })assets.clearDecodedImages();
}
static uint64_t committed() {
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
    if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)))throw std::runtime_error("GetProcessMemoryInfo failed");
    return memory.PrivateUsage;
}
static rep::ui::PlayerStatus wait(rep::ui::PlaybackController& controller,rep::ui::Phase expected) {
    auto deadline=rep::ui::Clock::now()+std::chrono::seconds(20);
    while(rep::ui::Clock::now()<deadline){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        auto status=controller.status();if(status.phase==expected)return status;
        if(status.phase==rep::ui::Phase::Error&&expected!=rep::ui::Phase::Error)throw std::runtime_error(rep::utf8(status.message));Sleep(2);}
    throw std::runtime_error("Playback status timed out");
}
static uint32_t capture(rep::ui::PlaybackController& controller) {
    auto serial=controller.status().captureSerial;controller.captureFrame();
    for(int n=0;n<2000;n++){auto status=controller.status();if(status.captureSerial>serial)return status.frameCrc;Sleep(2);}
    throw std::runtime_error("Frame capture timed out");
}
int wmain(int argc,wchar_t** argv) {try{
    if(argc!=4)throw std::runtime_error("mode root client required");
    std::filesystem::path root=argv[2],client=argv[3];
    if(std::wstring_view(argv[1])==L"assets"){
        rep::Assets assets(client/L"ImagePacks2");auto image=assets.preload("sprite/cache0/frame.img");auto frame=image->frame(0);
        std::weak_ptr<rep::Img> oldImage=image;std::weak_ptr<rep::Frame> oldFrame=frame;std::weak_ptr<rep::Pixels> oldPixels=frame->texture;
        auto crc=rep::crc(frame->texture->rgba);image.reset();frame.reset();clearDecoded(assets);
        bool released=oldImage.expired()&&oldFrame.expired()&&oldPixels.expired();auto reloaded=assets.frame("sprite/cache0/frame.img",0);
        std::cout<<"{\"released\":"<<(released?"true":"false")<<",\"pixel_crc_preserved\":"<<(crc==rep::crc(reloaded->texture->rgba)?"true":"false")<<"}\n";
    }else if(std::wstring_view(argv[1])==L"playback"){
        auto window=CreateWindowExW(0,L"STATIC",L"Playback cache regression",WS_OVERLAPPEDWINDOW,0,0,256,256,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window)throw std::runtime_error("Create regression canvas failed");
        uint64_t first=0,peak=0;uint32_t last=0;bool switched=true,replaySame=true,errorRecovered=false;std::vector<uint64_t> samples;
        {rep::ui::PlaybackController controller(window,root,client,false,rep::ui::Clock::now());
            for(int n=0;n<16;n++){
                auto path=client/L"Replay"/(L"cache"+std::to_wstring(n)+L".rep");controller.open(path);auto status=wait(controller,rep::ui::Phase::Ended);
                auto crc=capture(controller);if(n)switched&=crc!=last;last=crc;auto memory=committed();samples.push_back(memory);if(!n)first=memory;peak=std::max(peak,memory);
            }
            controller.replay();wait(controller,rep::ui::Phase::Ended);replaySame=capture(controller)==last;
            controller.open(client/L"Replay"/L"bad.rep");wait(controller,rep::ui::Phase::Error);
            controller.open(client/L"Replay"/L"cache0.rep");wait(controller,rep::ui::Phase::Ended);errorRecovered=capture(controller)!=last;
        }DestroyWindow(window);
        std::cout<<"{\"first_private_bytes\":"<<first<<",\"peak_private_bytes\":"<<peak<<",\"growth_bytes\":"<<peak-first<<",\"switched_pixels\":"<<(switched?"true":"false")<<",\"same_replay_pixels\":"<<(replaySame?"true":"false")<<",\"error_recovered\":"<<(errorRecovered?"true":"false")<<",\"samples\":[";
        for(size_t n=0;n<samples.size();n++){if(n)std::cout<<',';std::cout<<samples[n];}std::cout<<"]}\n";
    }else throw std::runtime_error("unknown regression mode");return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
