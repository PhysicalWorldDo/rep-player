#include "../src/audio.hpp"
#include <cmath>
#include <iostream>
#include <mutex>
#include <thread>

static std::mutex submissionsMutex;
static std::vector<float> submitted;
void observeActiveEndpointSubmission(const float* pcm,size_t count){std::lock_guard lock(submissionsMutex);submitted.insert(submitted.end(),pcm,pcm+count);}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=3)return 2;rep::Replay replay(argv[2]);rep::AudioTrack reference(argv[1],std::filesystem::path(argv[1])/L"cache",replay);reference.seek(0);
    std::vector<float> expected;for(int n=0;n<3;n++){std::vector<float> buffer(960);reference.render(buffer);expected.insert(expected.end(),buffer.begin(),buffer.end());}
    replay.rewind();rep::AudioPlayer player(std::make_unique<rep::AudioTrack>(argv[1],std::filesystem::path(argv[1])/L"cache",replay));player.volume(0,true);player.setRemainingPrefetchEnabled(false);auto serial=player.seek(0,false);
    for(int n=0;n<2000&&player.available()&&player.pcmReadySerial()<serial;n++)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(!player.available()||player.pcmReadySerial()<serial)throw rep::Error("No real prepared submission: "+player.error());
    std::lock_guard lock(submissionsMutex);if(submitted.size()!=expected.size())throw rep::Error("Expected three actual submitted buffers");
    size_t different=0,first=expected.size();double maximum=0;for(size_t n=0;n<expected.size();n++)if(expected[n]!=submitted[n]){different++;first=std::min(first,n);maximum=std::max(maximum,double(std::abs(expected[n]-submitted[n])));}
    std::cout<<"{\"samples\":"<<expected.size()<<",\"different\":"<<different<<",\"firstDifferentStereoSample\":"<<first/2<<",\"maximumError\":"<<maximum<<"}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what();return 1;}}
