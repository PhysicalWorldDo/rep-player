#include "../src/protocol.hpp"
#include <iostream>
#include <thread>
#if __has_include("../src/audio.hpp")
#include "../src/audio.hpp"
#define HAS_AUDIO 1
#endif
int wmain(int argc,wchar_t** argv){try{
#ifdef HAS_AUDIO
    if(argc<4)return 2;
    rep::ReplayOptions options;
    for(int n=4;n<argc;n++)if(std::wstring_view(argv[n])==L"dnf-july")options.profile=rep::ProtocolProfile::DnfJuly2026;
    rep::Replay replay(argv[2],options);
    auto owned=std::make_unique<rep::AudioTrack>(argv[1],std::filesystem::path(argv[1])/L"cache",replay);
    if(argc>4&&(std::wstring_view(argv[4])==L"player"||std::wstring_view(argv[4])==L"player-cold")){
        owned->prepare(0);rep::AudioPlayer player(std::move(owned));player.volume(0,true);player.play();
        auto wait=std::wstring_view(argv[4])==L"player-cold"?400:70;
        std::this_thread::sleep_for(std::chrono::milliseconds(wait));auto first=player.positionMilliseconds();player.pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));auto paused=player.positionMilliseconds();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));auto frozen=player.positionMilliseconds();
        player.resume(20);std::this_thread::sleep_for(std::chrono::milliseconds(30));auto resumed=player.positionMilliseconds();
        player.seek(10,false);std::this_thread::sleep_for(std::chrono::milliseconds(30));auto sought=player.positionMilliseconds();
        player.stop();std::this_thread::sleep_for(std::chrono::milliseconds(30));auto stopped=player.positionMilliseconds();
        std::cout<<"{\"implemented\":true,\"available\":"<<(player.available()?"true":"false")<<",\"first\":"<<first<<",\"paused\":"<<paused<<",\"frozen\":"<<frozen<<",\"resumed\":"<<resumed<<",\"sought\":"<<sought<<",\"stopped\":"<<stopped<<"}\n";return 0;
    }
    auto& track=*owned;
    auto seek=std::stoll(argv[3]);track.seek(seek);
    std::vector<float> pcm(4800*2);track.render(pcm);
    std::cout<<"{\"implemented\":true,\"events\":"<<track.eventCount()<<",\"missing\":"<<track.missingResources()<<",\"samples\":[";
    for(int ms=0;ms<100;ms++){if(ms)std::cout<<',';std::cout<<pcm[ms*48*2];}
    std::cout<<"]}\n";
#else
    std::cout<<"{\"implemented\":false,\"samples\":[]}\n";
#endif
    return 0;
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
