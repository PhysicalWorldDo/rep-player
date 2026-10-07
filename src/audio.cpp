#include "audio.hpp"
#include "audio_resources.hpp"
#include <windows.h>
#include <xaudio2.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

namespace rep {
namespace {
int64_t sampleAt(int64_t milliseconds){return std::max<int64_t>(0,milliseconds)*AudioTrack::SampleRate/1000;}
struct AudioEvent {
    int64_t sample=0;
    uint32_t opcode=0;
    std::array<int32_t,9> words{};
    bool flag=false;
    std::string tag;
};
}
struct AudioTrack::Impl {
    struct Voice {
        std::shared_ptr<const AudioClip> clip;
        const SoundDefinition* definition=nullptr;
        AudioEvent event;
        int64_t start=0;
        int slot=-1;
        bool detached=false;
        int64_t offset=0,forcedEnd=INT64_MAX;
    };
    std::shared_ptr<AudioResources> resources;
    std::vector<AudioEvent> events,pending;
    std::vector<Voice> voices;
    std::unordered_map<std::string,int> ranks,loopCounters;
    std::function<bool()> cancelled;
    std::vector<std::string> notes;
    size_t cursor=0;
    std::atomic<int64_t> position=0;
    int64_t duration=0;
    ProtocolProfile profile=ProtocolProfile::Dfo;

    bool stopped(const Voice& voice,int64_t at)const {
        auto length=int64_t(voice.clip->frames());if(!length)return true;
        auto age=at-voice.start;if(age<0)return false;
        if(at>=voice.forcedEnd)return true;
        if(voice.definition->loopDelay!=0)return age+voice.offset>=length;
        return voice.definition->loopCount>0&&age+voice.offset>=length*(int64_t(voice.definition->loopCount)+1);
    }
    int64_t end(const Voice& v)const {
        auto length=int64_t(v.clip->frames());int64_t natural=INT64_MAX;
        if(v.definition->loopDelay!=0)natural=v.start+length-v.offset;
        else if(v.definition->loopCount>0)natural=v.start+length*(int64_t(v.definition->loopCount)+1)-v.offset;
        return std::min(natural,v.forcedEnd);
    }
    void expire(int64_t at){
        for(size_t n=0;n<voices.size();){
            if(!stopped(voices[n],at)){n++;continue;}auto v=std::move(voices[n]);voices.erase(voices.begin()+n);
            if(v.forcedEnd!=INT64_MAX||v.definition->loopDelay<=0)continue;
            auto& count=loopCounters[v.event.tag];
            if(v.definition->loopCount>0&&count>=v.definition->loopCount-1){count=0;continue;}
            auto event=v.event;event.words[2]=0;event.words[5]=0;event.words[6]=ranks[event.tag];
            event.sample=end(v)+sampleAt(int64_t(v.definition->loopDelay));pending.push_back(std::move(event));count++;
        }
    }
    bool allowed(const AudioEvent& event,int64_t at){
        auto definition=resources->definition(event.tag);if(!definition||definition->duplicateLimit<=0)return true;
        auto active=std::count_if(voices.begin(),voices.end(),[&](const Voice& v){return v.event.tag==event.tag;});
        auto waiting=std::count_if(pending.begin(),pending.end(),[&](const AudioEvent& v){return v.tag==event.tag;});
        if(active+waiting<definition->duplicateLimit)return true;
        if(definition->kind==AudioKind::Effect&&definition->duplicatePolicy=="SWITCH"){
            for(auto& voice:voices)if(voice.event.tag==event.tag&&voice.forcedEnd==INT64_MAX){voice.forcedEnd=std::min(end(voice),at+sampleAt(100));return true;}
        }
        return false;
    }
    void startVoice(const AudioEvent& event,int64_t at){
        auto definition=resources->definition(event.tag);
        if(definition&&!definition->playable)return;
        auto clip=resources->clip(event.tag,cancelled);if(!clip||!clip->frames()||!definition)return;
        expire(at);if(!allowed(event,at))return;
        int slot=event.words[7];if(definition->kind==AudioKind::Music)slot=25;
        if(slot<0){
            for(int candidate=0;candidate<256;candidate++){
                if(candidate==14||candidate==15||candidate==25||candidate==38||candidate==39)continue;
                if(std::none_of(voices.begin(),voices.end(),[&](const Voice& v){return v.slot==candidate;})){slot=candidate;break;}
            }
            if(slot<0)return;
        }else{
            for(const auto& v:voices)if(v.slot==slot&&event.words[8]>=0&&v.event.words[8]>event.words[8])return;
            std::erase_if(voices,[&](const Voice& v){return v.slot==slot;});
        }
        auto offset=(definition->kind==AudioKind::Music||definition->kind==AudioKind::Ambient)?sampleAt(event.words[5]):0;
        voices.push_back({std::move(clip),definition,event,at,slot,false,offset,INT64_MAX});
    }
    void apply(const AudioEvent& event,int64_t at){
        if(event.opcode==6){
            // The native stop rank belongs to the shared TAG definition, so a
            // later request also changes the predicate for its older voices.
            ranks[event.tag]=event.words[6];
            if(event.words[2]>0){if(!allowed(event,at))return;auto later=event;later.sample=at+sampleAt(event.words[2]);pending.push_back(std::move(later));}
            else startVoice(event,at);
            return;
        }
        if(event.opcode==41){std::erase_if(voices,[](const Voice& v){return v.slot==25;});return;}
        auto type=uint32_t(event.words[0]);auto key=event.words[1],argument=event.words[2],threshold=event.words[4];
        auto matches=[&](const AudioEvent& request){
            if(request.words[3]!=key)return false;
            if(type==0)return request.words[4]==argument;
            if(type==2&&event.flag){auto d=resources->definition(request.tag);return d&&d->loopDelay>=0&&ranks[request.tag]<=threshold;}
            return true;
        };
        if(type==0||type==2){
            std::erase_if(voices,[&](const Voice& v){
                if(v.definition->kind==AudioKind::Ambient)return false;
                if(v.definition->kind==AudioKind::Music&&(type!=0||!dnfProfile(profile)))return false;
                return matches(v.event);
            });
            std::erase_if(pending,matches);
        }else if(type==1){
            // Native type1 detaches actor tracking but leaves the audible source
            // and its callback alive. The owner key is stored separately.
            for(auto& v:voices)if(v.definition->kind!=AudioKind::Music&&v.definition->kind!=AudioKind::Ambient&&v.event.words[3]==key)v.detached=true;
            std::erase_if(pending,matches);
        }else if(type==3){
            std::erase_if(voices,[](const Voice& v){return v.slot!=15&&v.slot!=25;});
            pending.clear();
        }
    }
    void boundary(int64_t at){
        expire(at);
        while(cursor<events.size()&&events[cursor].sample<=at)apply(events[cursor++],at);
        for(size_t n=0;n<pending.size();){
            if(pending[n].sample<=at){auto event=std::move(pending[n]);pending.erase(pending.begin()+n);startVoice(event,event.sample);}
            else n++;
        }
    }
    int64_t nextBoundary(int64_t limit)const {
        if(cursor<events.size())limit=std::min(limit,events[cursor].sample);
        for(const auto& event:pending)limit=std::min(limit,event.sample);
        for(const auto& voice:voices)limit=std::min(limit,end(voice));
        return limit;
    }
    void advance(int64_t target){
        auto at=position.load();boundary(at);
        while(at<target){auto next=nextBoundary(target);at=std::max(at,next);position=at;boundary(at);}
        position=target;expire(target);
    }
};
AudioTrack::AudioTrack(const std::filesystem::path& client,const std::filesystem::path& cache,Replay& replay,const std::function<bool()>& cancelled)
    :impl_(std::make_unique<Impl>()){
    impl_->resources=std::make_shared<AudioResources>(client,cache);impl_->cancelled=cancelled;impl_->profile=replay.options.profile;
    std::unordered_map<uint32_t,std::vector<AudioEvent>> commands;
    for(const auto& [id,command]:replay.dictionary){
        for(const auto& i:command.instructions){
            if(i.opcode!=6&&i.opcode!=7&&i.opcode!=41)continue;
            AudioEvent event;event.opcode=i.opcode;
            if(i.opcode==6){
                if(i.native.size()!=36)throw Error("Audio request was not decoded to the native ABI");
                std::memcpy(event.words.data(),i.native.data(),36);event.tag=replay.path(i.resource);
            }else if(i.opcode==7){
                std::memcpy(event.words.data(),i.native.data(),20);event.flag=i.native[12]!=0;event.words[4]=at<int32_t>(i.native,16);
            }
            commands[id].push_back(std::move(event));
        }
    }
    replay.rewind();Scene scene;
    try{while(replay.next(scene)){
        if(cancelled&&cancelled())throw Error("Audio preparation cancelled");
        impl_->duration=std::max<int64_t>(impl_->duration,scene.timestamp);
        for(auto id:scene.ids)if(auto found=commands.find(id);found!=commands.end())for(auto event:found->second){event.sample=sampleAt(scene.timestamp);impl_->events.push_back(std::move(event));}
    }}catch(...){replay.rewind();throw;}
    replay.rewind();
    std::stable_sort(impl_->events.begin(),impl_->events.end(),[](const AudioEvent& a,const AudioEvent& b){return a.sample<b.sample;});
}
AudioTrack::~AudioTrack()=default;
bool AudioTrack::hasEvents()const{return !impl_->events.empty();}
uint64_t AudioTrack::eventCount()const{return impl_->events.size();}
uint64_t AudioTrack::missingResources()const{return impl_->resources->missing();}
int64_t AudioTrack::positionMilliseconds()const{return impl_->position.load()*1000/SampleRate;}
int64_t AudioTrack::durationMilliseconds()const{return impl_->duration;}
std::vector<std::string> AudioTrack::diagnostics()const{auto notes=impl_->resources->diagnostics();notes.insert(notes.end(),impl_->notes.begin(),impl_->notes.end());return notes;}
void AudioTrack::prepare(int64_t milliseconds,int64_t horizon,const std::function<bool()>& cancelled){
    auto first=sampleAt(milliseconds),last=sampleAt(milliseconds+std::max<int64_t>(0,horizon));
    std::vector<std::string> tags;std::unordered_map<std::string,bool> seen;
    for(const auto& event:impl_->events)if(event.opcode==6&&event.sample>=first&&event.sample<=last){
        auto d=impl_->resources->definition(event.tag);if(d&&!d->playable)continue;
        if(seen.emplace(event.tag,true).second)tags.push_back(event.tag);
    }
    std::atomic<size_t> next=0;std::vector<std::future<void>> jobs;
    for(size_t n=0;n<std::min<size_t>(4,tags.size());n++)jobs.push_back(std::async(std::launch::async,[&]{
        for(size_t k;(k=next++)<tags.size();){if(cancelled&&cancelled())return;impl_->resources->clip(tags[k],cancelled?cancelled:impl_->cancelled);}
    }));
    for(auto& job:jobs)job.get();
}
void AudioTrack::seek(int64_t milliseconds){
    impl_->cursor=0;impl_->position=0;impl_->voices.clear();impl_->pending.clear();impl_->ranks.clear();impl_->loopCounters.clear();impl_->advance(sampleAt(milliseconds));
}
void AudioTrack::render(std::span<float> output){
    if(output.size()%2)throw Error("Audio buffer must contain stereo frames");
    std::fill(output.begin(),output.end(),0.f);size_t completed=0,total=output.size()/2;
    while(completed<total){
        int64_t start=impl_->position.load();impl_->boundary(start);
        auto count=size_t(std::max<int64_t>(1,impl_->nextBoundary(start+int64_t(total-completed))-start));
        count=std::min(count,total-completed);
        for(const auto& voice:impl_->voices){
            auto length=int64_t(voice.clip->frames());
            for(size_t n=0;n<count;n++){
                auto time=start+int64_t(n);if(impl_->stopped(voice,time))break;
                auto age=time-voice.start;if(age<0)continue;
                auto source=voice.definition->loopDelay==0?(age+voice.offset)%length:age+voice.offset;if(source>=length)continue;
                for(int channel=0;channel<2;channel++)output[(completed+n)*2+channel]+=voice.clip->samples[size_t(source)*2+channel]*voice.definition->gain;
            }
        }
        completed+=count;impl_->position=start+int64_t(count);
    }
    for(float& value:output)value=std::clamp(value,-1.f,1.f);
}

struct AudioPlayer::Impl {
    using Clock=std::chrono::steady_clock;
    std::unique_ptr<AudioTrack> track;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable condition;
    bool quit=false,ready=false,playing=false,seekRequested=false;
    int64_t requested=0;
    float volume=1;
    bool muted=false;
    std::atomic<bool> available=false;
    std::atomic<int64_t> position=0;
    std::string failure;
    void run(){
        auto co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);IXAudio2* engine=nullptr;IXAudio2MasteringVoice* master=nullptr;IXAudio2SourceVoice* voice=nullptr;
        std::future<void> prefetch;std::atomic<bool> cancelPrefetch=false;
        auto close=[&]{if(voice)voice->DestroyVoice();if(master)master->DestroyVoice();if(engine)engine->Release();if(SUCCEEDED(co))CoUninitialize();};
        try{
            if(FAILED(XAudio2Create(&engine,0,XAUDIO2_DEFAULT_PROCESSOR))||FAILED(engine->CreateMasteringVoice(&master)))throw Error("Default audio output device is unavailable");
            WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;format.nChannels=2;format.nSamplesPerSec=AudioTrack::SampleRate;
            format.wBitsPerSample=32;format.nBlockAlign=8;format.nAvgBytesPerSec=format.nSamplesPerSec*8;
            auto create=[&]{if(FAILED(engine->CreateSourceVoice(&voice,&format,0,1,nullptr)))throw Error("Cannot create REP audio output voice");};create();
            // Decoding ahead must never wait on the 30ms device fill queue.
            prefetch=std::async(std::launch::async,[&]{track->prepare(0,track->durationMilliseconds(),[&]{std::lock_guard lock(mutex);return quit||cancelPrefetch.load();});});
            {std::lock_guard lock(mutex);available=true;ready=true;condition.notify_all();}
            std::array<std::array<float,960>,4> buffers{};size_t bufferIndex=0;bool active=false;int64_t base=0;
            while(true){
                bool wanted,seek;int64_t target;float gain;
                {std::unique_lock lock(mutex);if(quit)break;wanted=playing;seek=std::exchange(seekRequested,false);target=requested;gain=muted?0.f:volume;
                    if(!wanted&&!seek){if(active){voice->Stop();active=false;}condition.wait_for(lock,std::chrono::milliseconds(10));continue;}}
                voice->SetVolume(gain);
                if(seek){
                    // DestroyVoice waits until buffers are no longer consumed.
                    // Re-creation also supplies a fresh, unambiguous sample clock.
                    voice->DestroyVoice();voice=nullptr;create();voice->SetVolume(gain);
                    track->seek(target);base=target;bufferIndex=0;active=false;position=target;
                }
                if(wanted&&!active){voice->Start();active=true;}
                if(!wanted)continue;
                XAUDIO2_VOICE_STATE state{};voice->GetState(&state);
                position=base+int64_t(state.SamplesPlayed)*1000/AudioTrack::SampleRate;
                if(state.BuffersQueued<3){
                    auto& buffer=buffers[bufferIndex++%buffers.size()];track->render(buffer);
                    XAUDIO2_BUFFER submission{};submission.AudioBytes=DWORD(buffer.size()*sizeof(float));submission.pAudioData=reinterpret_cast<BYTE*>(buffer.data());
                    if(FAILED(voice->SubmitSourceBuffer(&submission)))throw Error("REP audio output rejected PCM buffer");
                }else{
                    std::unique_lock lock(mutex);condition.wait_for(lock,std::chrono::milliseconds(2));
                }
            }
        }catch(const std::exception& e){std::lock_guard lock(mutex);failure=e.what();available=false;ready=true;condition.notify_all();}
        cancelPrefetch=true;if(prefetch.valid())try{prefetch.get();}catch(...){}
        close();
    }
};
AudioPlayer::AudioPlayer(std::unique_ptr<AudioTrack> track):impl_(std::make_unique<Impl>()){
    impl_->track=std::move(track);impl_->thread=std::thread([this]{impl_->run();});
    std::unique_lock lock(impl_->mutex);impl_->condition.wait(lock,[&]{return impl_->ready;});
}
AudioPlayer::~AudioPlayer(){if(!impl_)return;{std::lock_guard lock(impl_->mutex);impl_->quit=true;impl_->condition.notify_all();}if(impl_->thread.joinable())impl_->thread.join();}
void AudioPlayer::play(int64_t milliseconds){seek(milliseconds,true);}
void AudioPlayer::pause(){std::lock_guard lock(impl_->mutex);impl_->playing=false;impl_->condition.notify_all();}
void AudioPlayer::resume(int64_t milliseconds){seek(milliseconds,true);}
void AudioPlayer::stop(){std::lock_guard lock(impl_->mutex);impl_->playing=false;impl_->seekRequested=true;impl_->requested=0;impl_->condition.notify_all();}
void AudioPlayer::seek(int64_t milliseconds,bool playing){std::lock_guard lock(impl_->mutex);impl_->requested=std::max<int64_t>(0,milliseconds);impl_->playing=playing;impl_->seekRequested=true;impl_->condition.notify_all();}
void AudioPlayer::volume(float value,bool muted){std::lock_guard lock(impl_->mutex);impl_->volume=std::clamp(value,0.f,1.f);impl_->muted=muted;impl_->condition.notify_all();}
bool AudioPlayer::available()const{return impl_->available;}
std::string AudioPlayer::error()const{std::lock_guard lock(impl_->mutex);return impl_->failure;}
uint64_t AudioPlayer::missingResources()const{return impl_->track->missingResources();}
uint64_t AudioPlayer::eventCount()const{return impl_->track->eventCount();}
int64_t AudioPlayer::positionMilliseconds()const{return impl_->position;}
}
