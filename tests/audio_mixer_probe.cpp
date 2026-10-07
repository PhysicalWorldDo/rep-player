#include "../src/protocol.hpp"
#include <iostream>
#if __has_include("../src/audio.hpp")
#include "../src/audio.hpp"
#define HAS_AUDIO 1
#endif
int wmain(int argc,wchar_t** argv){try{
#ifdef HAS_AUDIO
    if(argc<4)return 2;
    rep::Replay replay(argv[2]);
    rep::AudioTrack track(argv[1],std::filesystem::path(argv[1])/L"cache",replay);
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
