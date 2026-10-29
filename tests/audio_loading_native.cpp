#include "audio.hpp"
#include <iostream>
#include <thread>
#include <future>
#if __has_include("retained_cache.hpp")
#include "retained_cache.hpp"
#define HAS_RETAINED_CACHE 1
#endif
static std::atomic<bool> startGate=false,startEntered=false,startReleased=false,afterEntered=false,afterReleased=false;
static std::atomic<float> actualDeviceGain=-1,actualDeviceRate=-1;
static std::atomic<int> concurrentDecodes=0,maxConcurrentDecodes=0;
static std::atomic<bool> gateDecodes=false;
static std::atomic<int> batchGate=0,batchCalls=0;
static std::atomic<bool> batchEntered=false,batchReleased=false;
void audioBeforeDeviceStart(){if(!startGate)return;startEntered=true;while(!startReleased)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
void audioAfterDeviceStart(){if(!startGate)return;afterEntered=true;while(!afterReleased)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
void audioDeviceSettings(float gain,float rate){actualDeviceGain=gain;actualDeviceRate=rate;}
void audioDecodeStarted(){int active=++concurrentDecodes;int prior=maxConcurrentDecodes.load();while(prior<active&&!maxConcurrentDecodes.compare_exchange_weak(prior,active)){}if(gateDecodes){auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);while(concurrentDecodes<4&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
void audioDecodeEnded(){--concurrentDecodes;}
void audioBatchBoundary(int phase){if(phase==1)++batchCalls;if(batchGate!=phase)return;batchEntered=true;while(!batchReleased)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
template<class P> static bool pcmReady(P& player,uint64_t serial){if constexpr(requires{player.pcmReadySerial();})return player.pcmReadySerial()>=serial;else return false;}
template<class T> static const void* eventIdentity(T& track){if constexpr(requires{track.eventIdentity();})return track.eventIdentity();else return nullptr;}
template<class P> static bool buffering(P& player){if constexpr(requires{player.stats().buffering;})return player.stats().buffering;else return false;}
template<class P> static bool freeze(P& player,bool value){if constexpr(requires{player.pauseBuffering(value);}){player.pauseBuffering(value);return true;}else return false;}
template<class P> static size_t buffers(P& player){if constexpr(requires{player.stats().bufferedMilliseconds;})return size_t(player.stats().bufferedMilliseconds);else return 0;}
template<class P> static std::vector<std::string> playerNotes(P& player){if constexpr(requires{player.diagnostics();})return player.diagnostics();else return {};}
template<class P> static bool remainingEnabled(P& player,bool enabled){if constexpr(requires{player.setRemainingPrefetchEnabled(enabled);}){player.setRemainingPrefetchEnabled(enabled);return true;}else return false;}
template<class R> static auto acquireMany(R& resources,const std::vector<std::string>& tags){if constexpr(requires{resources.prepareClips(tags);})return resources.prepareClips(tags);else{std::vector<std::shared_ptr<const rep::AudioClip>> out(tags.size());std::vector<std::future<void>> jobs;for(size_t n=0;n<tags.size();n++)jobs.push_back(std::async(std::launch::async,[&,n]{out[n]=resources.clip(tags[n]);}));for(auto& job:jobs)job.get();return out;}}
template<class R> static auto acquireManyCancelled(R& resources,const std::vector<std::string>& tags,const std::function<bool()>& cancelled){if constexpr(requires{resources.prepareClips(tags,cancelled);})return resources.prepareClips(tags,cancelled);else{std::vector<std::shared_ptr<const rep::AudioClip>> out;for(auto& tag:tags)out.push_back(resources.clip(tag,cancelled));return out;}}
template<class R> static uint64_t processes(R& resources){if constexpr(requires{resources.decoderProcesses();})return resources.decoderProcesses();else return resources.decoded();}
template<class T> static uint64_t trackProcesses(T& track){if constexpr(requires{track.decoderProcesses();})return track.decoderProcesses();else return 0;}
template<class F> static bool wait(F predicate,int timeout=5000){auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);while(std::chrono::steady_clock::now()<end){if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return false;}
int wmain(int argc,wchar_t** argv){if(argc!=5)return 2;try{std::string mode=rep::utf8(argv[1]);std::filesystem::path client=argv[2],replayPath=argv[3],cachePath=argv[4];auto index=rep::AudioResources::buildIndex(client,true);
#ifdef HAS_RETAINED_CACHE
    auto cache=std::make_shared<rep::RetainedCache>(2*1024*1024);
    auto resources=[&]<class T=rep::AudioResources>(){if constexpr(std::is_constructible_v<T,rep::AudioResources::IndexPtr,std::shared_ptr<rep::RetainedCache>,std::filesystem::path>)return std::make_unique<T>(index,cache,cachePath);else if constexpr(std::is_constructible_v<T,rep::AudioResources::IndexPtr,std::shared_ptr<rep::RetainedCache>>)return std::make_unique<T>(index,cache);else return std::make_unique<T>(index);};
    auto track=[&]<class T=rep::AudioTrack>(rep::Replay& replay){if constexpr(std::is_constructible_v<T,const std::filesystem::path&,const std::filesystem::path&,rep::Replay&,const std::function<bool()>&,rep::AudioResources::IndexPtr,std::shared_ptr<rep::RetainedCache>>)return std::make_unique<T>(client,cachePath,replay,std::function<bool()>{},index,cache);else return std::make_unique<T>(client,cachePath,replay,std::function<bool()>{},index);};
#else
    auto resources=[&]{return std::make_unique<rep::AudioResources>(index);};auto track=[&](rep::Replay& replay){return std::make_unique<rep::AudioTrack>(client,cachePath,replay,std::function<bool()>{},index);};
#endif
    bool passed=false;
    if(mode=="shared_pcm"){
        auto a=resources(),b=resources();auto left=std::async(std::launch::async,[&]{return a->clip("A");});auto right=std::async(std::launch::async,[&]{return b->clip("ALIAS");});auto x=left.get(),y=right.get();passed=x&&x==y&&a->decoded()+b->decoded()==1;
    }else if(mode=="event_cache"){
        rep::Replay first(replayPath),second(first);auto a=track(first),b=track(second);passed=eventIdentity(*a)&&eventIdentity(*a)==eventIdentity(*b)&&a->eventCount()==3&&b->eventCount()==3;
    }else if(mode=="pcm_gate"){
        rep::Replay replay(replayPath);auto owned=track(replay);rep::AudioPlayer player(std::move(owned));player.volume(0,true);auto serial=player.seek(0,false);bool ready=wait([&]{return pcmReady(player,serial);});auto before=player.positionMilliseconds();std::this_thread::sleep_for(std::chrono::milliseconds(50));bool still=player.positionMilliseconds()==before;auto buffered=buffers(player);bool resumed=player.resumePrepared(serial);bool progressed=wait([&]{return player.positionMilliseconds()>=20;});passed=ready&&still&&buffered>=30&&resumed&&progressed;
    }else if(mode=="freeze_clock"){
        rep::Replay replay(replayPath);rep::AudioPlayer player(track(replay));player.volume(0,true);auto serial=player.seek(0,true);bool played=wait([&]{return player.positionMilliseconds()>=20;});bool available=freeze(player,true);bool frozen=wait([&]{return buffering(player);});std::this_thread::sleep_for(std::chrono::milliseconds(25));auto at=player.positionMilliseconds();std::this_thread::sleep_for(std::chrono::milliseconds(45));bool still=at==player.positionMilliseconds();freeze(player,false);bool resumed=wait([&]{return player.positionMilliseconds()>at;});passed=played&&available&&frozen&&still&&resumed;
    }else if(mode=="cancel_retry"){
        auto a=resources(),b=resources();auto cancelled=a->clip("A",[]{return true;});auto retried=b->clip("A");passed=!cancelled&&retried&&b->missing()==0;
    }else if(mode=="old_serial"){
        rep::Replay replay(replayPath);rep::AudioPlayer player(track(replay));auto first=player.seek(900,false);auto second=player.seek(0,false);bool ready=wait([&]{return pcmReady(player,second);});passed=ready&&!player.resumePrepared(first)&&player.positionMilliseconds()==0;
    }else if(mode=="cancelled_owner"){
        auto a=resources(),b=resources();std::atomic<int> ownerPolls=0,waiterPolls=0;std::atomic<bool> ownerEntered=false,releaseOwner=false;
        auto first=std::async(std::launch::async,[&]{return a->clip("A",[&]{if(++ownerPolls>=2){ownerEntered=true;while(!releaseOwner)std::this_thread::sleep_for(std::chrono::milliseconds(1));return true;}return false;});});
        bool entered=wait([&]{return ownerEntered.load();});
        auto second=std::async(std::launch::async,[&]{return b->clip("ALIAS",[&]{++waiterPolls;return false;});});
        bool waiting=wait([&]{return waiterPolls.load()>=2;});releaseOwner=true;auto x=first.get(),y=second.get();passed=entered&&waiting&&!x&&y&&b->decoded()==1&&a->missing()+b->missing()==0;
    }else if(mode=="bad_patch_shared"){
        auto a=resources(),b=resources();auto x=a->clip("P"),y=b->clip("P");passed=x&&x==y&&a->decoded()+b->decoded()==1&&a->missing()+b->missing()==0;
    }else if(mode=="one_shot_cancel"){
        rep::Replay replay(replayPath);rep::AudioPlayer player(track(replay));std::atomic<int> polls=0;auto serial=player.seek(900,true,[&]{return ++polls==2;});bool entered=wait([&]{return polls.load()>=2;});std::this_thread::sleep_for(std::chrono::milliseconds(80));bool rejected=player.appliedSeekSerial()<serial&&!pcmReady(player,serial)&&player.positionMilliseconds()==0&&!player.resumePrepared(serial);auto next=player.seek(37,false);bool recovered=wait([&]{return pcmReady(player,next);});passed=entered&&rejected&&recovered&&player.positionMilliseconds()==37;
    }else if(mode=="freeze_start_race"){
        rep::Replay replay(replayPath);rep::AudioPlayer player(track(replay));player.volume(0,true);auto serial=player.seek(0,false);bool ready=wait([&]{return pcmReady(player,serial);});startGate=true;player.resumePrepared(serial);bool entered=wait([&]{return startEntered.load();});player.pauseBuffering(true);bool acknowledged=buffering(player);startReleased=true;bool after=wait([&]{return afterEntered.load();});bool stopped=buffering(player)&&!player.stats().clockRunning;afterReleased=true;startGate=false;passed=ready&&entered&&after&&acknowledged&&stopped;
    }else if(mode=="bad_official"){
        auto broken=rep::AudioResources::buildIndex(client/L"broken",true);rep::AudioResources resource(broken);try{auto clip=resource.clip("P");passed=!clip&&resource.missing()==1;}catch(const std::exception&){passed=false;}
    }else if(mode=="underrun_counter"){
        rep::Replay replay(replayPath);rep::AudioPlayer player(track(replay));player.volume(0,true);auto serial=player.seek(0,false);bool ready=wait([&]{return pcmReady(player,serial);});startGate=true;player.resumePrepared(serial);bool entered=wait([&]{return startEntered.load();});startReleased=true;bool after=wait([&]{return afterEntered.load();});std::this_thread::sleep_for(std::chrono::milliseconds(90));afterReleased=true;startGate=false;bool counted=wait([&]{return player.stats().underruns>=1;});passed=ready&&entered&&after&&counted;
    }else if(mode=="fresh_start_settings"){
        rep::Replay replay(replayPath);rep::AudioPlayer player(track(replay));auto serial=player.seek(0,false);bool ready=wait([&]{return pcmReady(player,serial);});startGate=true;player.resumePrepared(serial);bool entered=wait([&]{return startEntered.load();});player.volume(.25f,true);player.setPlaybackRate(2);startReleased=true;bool after=wait([&]{return afterEntered.load();});bool fresh=actualDeviceGain.load()==0&&actualDeviceRate.load()==2;player.pause();afterReleased=true;startGate=false;passed=ready&&entered&&after&&fresh;if(!passed)std::cerr<<"ready="<<ready<<" entered="<<entered<<" after="<<after<<" gain="<<actualDeviceGain.load()<<" rate="<<actualDeviceRate.load()<<"\n";
    }else if(mode=="first_missing_diagnostic"){
        rep::Replay replay(replayPath);auto broken=rep::AudioResources::buildIndex(client/L"broken",true);auto owned=std::make_unique<rep::AudioTrack>(client/L"broken",cachePath,replay,std::function<bool()>{},broken);rep::AudioPlayer player(std::move(owned));auto serial=player.seek(0,false);bool ready=wait([&]{return pcmReady(player,serial);});auto notes=playerNotes(player);passed=ready&&player.missingResources()>0&&std::any_of(notes.begin(),notes.end(),[](auto& value){return value.find("Unknown audio TAG: A")!=std::string::npos;});
    }else if(mode=="parallel_seek"){
        rep::Replay replay(replayPath.parent_path()/L"parallel.rep");auto owned=track(replay);gateDecodes=true;bool sought=owned->seek(1000);gateDecodes=false;std::array<float,960> output{};owned->render(output);passed=sought&&(maxConcurrentDecodes>=3||trackProcesses(*owned)==1)&&owned->eventCount()==4&&std::abs(output[0]-1.f)<.0001f&&owned->missingResources()==0;
    }else if(mode=="batch_pcm_exact"||mode=="batch_multitrack"||mode=="batch_bad_patch"||mode=="batch_formats_pcm"||mode=="batch_wave_formats"||mode=="batch_bad_clip"){
        auto a=resources();std::vector<std::string> tags={"F0","F1","F2","F3"};if(mode=="batch_multitrack")tags.push_back("MULTI");if(mode=="batch_bad_patch")tags.back()="P";if(mode=="batch_bad_clip")tags.back()="NAN";if(mode=="batch_formats_pcm")tags={"F0","F1","F2","F3","OGG","FLAC","MP3","OPUS","COMPRESSED"};if(mode=="batch_wave_formats")tags={"FLOAT","FLOAT64","EXTPCM","EXTFLOAT"};auto clips=acquireMany(*a,tags);rep::AudioResources reference(rep::AudioResources::buildIndex(client,true));bool exact=clips.size()==tags.size();for(size_t n=0;n<tags.size()&&exact;n++){auto expected=reference.clip(tags[n]);exact=(!expected&&!clips[n])||(expected&&clips[n]&&expected->samples.size()==clips[n]->samples.size()&&!std::memcmp(expected->samples.data(),clips[n]->samples.data(),expected->samples.size()*sizeof(float)));if(!exact)std::cerr<<"PCM mismatch "<<tags[n]<<" expected="<<(expected?expected->samples.size():0)<<" actual="<<(clips[n]?clips[n]->samples.size():0)<<"\n";}bool failedBatch=mode=="batch_bad_patch"||mode=="batch_bad_clip";bool batched=failedBatch?batchCalls>0:processes(*a)<tags.size();passed=exact&&batched&&a->missing()==(mode=="batch_bad_clip"?1:0);if(mode=="batch_multitrack")passed&=clips.back()&&clips.back()->samples[0]>.49f;if(mode=="batch_formats_pcm")passed&=processes(*a)==3;
    }else if(mode=="batch_cancel_owner"||mode=="batch_cancel_process"){
        auto a=resources(),b=resources();std::atomic<bool> cancel=false;std::atomic<int> waiterPolls=0;batchGate=mode=="batch_cancel_owner"?1:2;std::vector<std::string> tags={"F0","F1","F2","F3"};auto owner=std::async(std::launch::async,[&]{return acquireManyCancelled(*a,tags,[&]{return cancel.load();});});bool entered=wait([&]{return batchEntered.load();});auto waiter=std::async(std::launch::async,[&]{return b->clip("F0",[&]{++waiterPolls;return false;});});bool waiting=wait([&]{return waiterPolls>=2;});cancel=true;batchReleased=true;auto cancelled=owner.get();auto recovered=waiter.get();passed=entered&&waiting&&batchCalls>0&&std::none_of(cancelled.begin(),cancelled.end(),[](auto& clip){return bool(clip);})&&recovered&&a->missing()+b->missing()==0;auto directory=cachePath/L"audio-batches";passed&=!std::filesystem::exists(directory)||std::filesystem::is_empty(directory);
    }else if(mode=="paused_remaining_prefetch"||mode=="remaining_seek_priority"||mode=="remaining_first_picture_gate"){
#ifdef HAS_RETAINED_CACHE
        cache->trim(16*1024*1024);
#endif
        rep::Replay replay(replayPath.parent_path()/L"far.rep");auto observer=resources();if(mode=="remaining_seek_priority")batchGate=2;rep::AudioPlayer player(track(replay));bool gate=mode!="remaining_first_picture_gate"||remainingEnabled(player,false);auto serial=player.seek(0,false);bool ready=wait([&]{return pcmReady(player,serial);});
        if(mode=="paused_remaining_prefetch"){bool far=wait([&]{for(int n=0;n<4;n++)if(!observer->prepared("F"+std::to_string(n)).ready)return false;return true;});passed=ready&&far&&player.positionMilliseconds()==0&&!player.stats().clockRunning;}
        else if(mode=="remaining_seek_priority"){bool entered=wait([&]{return batchEntered.load();});player.pauseBuffering(true);auto latest=player.seek(37,false);batchReleased=true;bool latestReady=wait([&]{return pcmReady(player,latest);});bool clean=true;for(int n=0;n<4;n++)clean&=!observer->prepared("F"+std::to_string(n)).ready;passed=ready&&entered&&latestReady&&player.positionMilliseconds()==37&&!player.resumePrepared(serial)&&clean;}
        else {std::this_thread::sleep_for(std::chrono::milliseconds(150));bool blocked=true;for(int n=0;n<4;n++)blocked&=!observer->prepared("F"+std::to_string(n)).ready;remainingEnabled(player,true);bool warmed=wait([&]{for(int n=0;n<4;n++)if(!observer->prepared("F"+std::to_string(n)).ready)return false;return true;});passed=gate&&ready&&blocked&&warmed&&player.positionMilliseconds()==0&&!player.stats().clockRunning;}
#ifdef HAS_RETAINED_CACHE
    }else if(mode=="zero_budget_pin"){
        auto a=resources();cache->trim(0);auto x=a->clip("A");cache->trim();auto active=cache->stats();auto y=a->clip("ALIAS");bool pinned=x&&x==y&&active.activeBytes>0&&active.inactiveBytes==0&&a->decoded()==1;x.reset();y.reset();cache->trim();bool bounded=cache->stats().inactiveBytes==0;auto z=a->clip("A");passed=pinned&&bounded&&z&&a->decoded()==2;
#endif
    }
    std::cout<<"{\"mode\":\""<<mode<<"\",\"pass\":"<<(passed?"true":"false")<<"}";return passed?0:1;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 2;}}
