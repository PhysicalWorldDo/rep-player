#include "audio.hpp"
#include "audio_resources.hpp"
#include "retained_cache.hpp"
#include <windows.h>
#include <xaudio2.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace rep {
namespace {
int64_t sampleAt(int64_t milliseconds){return std::max<int64_t>(0,milliseconds)*AudioTrack::SampleRate/1000;}
bool specialSlot(int slot){
    switch(slot){case 14:case 15:case 25:case 35:case 36:case 37:case 38:case 39:return true;default:return false;}
}
struct AudioEvent {
    int64_t sample=0;
    uint64_t ordinal=0;
    uint32_t opcode=0;
    std::array<int32_t,9> words{};
    bool flag=false;
    std::string tag;
};
struct AudioSeekCancelled {};
struct AudioTimeline {
    std::weak_ptr<const void> identity;
    std::vector<AudioEvent> events;
    int64_t duration=0;
};
std::string oggPath(std::string path){
    path=canonical(std::move(path));while(!path.empty()&&path.front()=='/')path.erase(path.begin());
    return path.size()>=4&&path.compare(path.size()-4,4,".ogg")==0?path:std::string{};
}
std::unordered_set<std::string> hiddenOggPaths(std::unordered_set<std::string> paths){
    std::unordered_set<std::string> result;for(auto& path:paths)if(auto normalized=oggPath(path);!normalized.empty())result.insert(std::move(normalized));return result;
}
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
    std::shared_ptr<const AudioClip> singleClip;
    std::vector<std::string> allOggs;
    std::unordered_map<uint64_t,std::vector<std::string>> recordOggs;
    std::unordered_map<std::string,std::string> oggTags;
    std::unordered_set<std::string> hiddenOggs;
    std::shared_ptr<const AudioTimeline> timeline;
    std::span<const AudioEvent> events;
    std::vector<AudioEvent> pending;
    bool preparedOnly=false;
    std::unordered_map<std::string,std::shared_ptr<const AudioClip>> preparedClips;
    std::vector<Voice> voices;
    std::unordered_map<std::string,int> ranks,loopCounters;
    std::function<bool()> cancelled,seekCancelled;
    std::vector<std::string> notes;
    size_t cursor=0;
    std::atomic<int64_t> position=0;
    std::optional<int64_t> trackedEndBoundary;
    int64_t duration=0;
    ProtocolProfile profile=ProtocolProfile::Dfo;

    void indexOggCalls(){
        for(const auto& event:events){
            if(event.opcode!=6)continue;auto definition=resources->definition(event.tag);if(!definition||!definition->playable)continue;
            auto path=oggPath(definition->file);if(path.empty())continue;
            recordOggs[event.ordinal].push_back(path);oggTags.try_emplace(std::move(path),event.tag);
        }
        for(const auto& [path,tag]:oggTags)allOggs.push_back(path);std::sort(allOggs.begin(),allOggs.end());
        for(auto& [ordinal,paths]:recordOggs){std::sort(paths.begin(),paths.end());paths.erase(std::unique(paths.begin(),paths.end()),paths.end());}
    }

    void checkSeekCancelled()const{if(seekCancelled&&seekCancelled())throw AudioSeekCancelled{};}

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
            checkSeekCancelled();
            if(!stopped(voices[n],at)){n++;continue;}auto v=std::move(voices[n]);voices.erase(voices.begin()+n);
            if(v.forcedEnd!=INT64_MAX||v.definition->loopDelay<=0)continue;
            auto& count=loopCounters[v.event.tag];
            if(v.definition->loopCount>0&&count>=v.definition->loopCount-1){count=0;continue;}
            auto event=v.event;event.words[2]=0;event.words[5]=0;event.words[6]=ranks[event.tag];
            event.sample=end(v)+sampleAt(int64_t(v.definition->loopDelay));pending.push_back(std::move(event));count++;
        }
    }
    bool allowed(const AudioEvent& event,int64_t at,int slot){
        auto definition=resources->definition(event.tag);if(!definition||specialSlot(slot))return true;
        // Native manager default is three; an absent/zero XML limit inherits it.
        const int limit=definition->duplicateLimit>0?definition->duplicateLimit:3;
        auto active=std::count_if(voices.begin(),voices.end(),[&](const Voice& v){return v.event.tag==event.tag;});
        auto waiting=std::count_if(pending.begin(),pending.end(),[&](const AudioEvent& v){return v.tag==event.tag;});
        if(active+waiting<limit)return true;
        if(definition->kind==AudioKind::Effect&&definition->duplicatePolicy=="SWITCH"){
            for(auto& voice:voices)if(voice.event.tag==event.tag){
                voice.forcedEnd=INT64_MAX;voice.forcedEnd=std::min(end(voice),at+sampleAt(100));break;
            }
            return true;
        }
        return false;
    }
    void startVoice(const AudioEvent& event,int64_t at){
        auto definition=resources->definition(event.tag);
        if(definition&&!definition->playable)return;
        auto clip=preparedOnly?(preparedClips.contains(event.tag)?preparedClips.at(event.tag):std::shared_ptr<const AudioClip>{}):resources->clip(event.tag,[&]{return (seekCancelled&&seekCancelled())||(cancelled&&cancelled());});
        checkSeekCancelled();if(!clip||!clip->frames()||!definition)return;
        int slot=event.words[7];if(definition->kind==AudioKind::Music)slot=25;else if(definition->kind==AudioKind::Ambient)slot=15;
        expire(at);if(!allowed(event,at,slot))return;
        if(slot<0){
            for(int candidate=0;candidate<256;candidate++){
                if(specialSlot(candidate))continue;
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
            // Native delayed requests enter the queue before the start-time limit check.
            if(event.words[2]>0){auto later=event;later.sample=at+sampleAt(event.words[2]);pending.push_back(std::move(later));}
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
        while(cursor<events.size()&&events[cursor].sample<=at){checkSeekCancelled();apply(events[cursor++],at);}
        for(size_t n=0;n<pending.size();){
            checkSeekCancelled();
            if(pending[n].sample<=at){auto event=std::move(pending[n]);pending.erase(pending.begin()+n);startVoice(event,event.sample);}
            else n++;
        }
    }
    void renderBoundary(int64_t at){
        // Tracking may already have consumed the previous buffer's end here.
        // Skip only that added call; ordinary seek/render ordering is retained.
        bool consumed=trackedEndBoundary&&*trackedEndBoundary==at;trackedEndBoundary.reset();if(!consumed)boundary(at);
    }
    int64_t nextBoundary(int64_t limit)const {
        if(cursor<events.size())limit=std::min(limit,events[cursor].sample);
        for(const auto& event:pending){checkSeekCancelled();limit=std::min(limit,event.sample);}
        for(const auto& voice:voices){checkSeekCancelled();limit=std::min(limit,end(voice));}
        return limit;
    }
    void advance(int64_t target){
        checkSeekCancelled();auto at=position.load();boundary(at);
        while(at<target){checkSeekCancelled();auto next=nextBoundary(target);at=std::max(at,next);position=at;boundary(at);}
        position=target;expire(target);
    }
};
AudioTrack::AudioTrack(const std::filesystem::path& client,const std::filesystem::path& cache,Replay& replay,const std::function<bool()>& cancelled,AudioResources::IndexPtr index,std::shared_ptr<RetainedCache> cacheStore)
    :impl_(std::make_unique<Impl>()){
    impl_->cancelled=cancelled;impl_->profile=replay.options.profile;
    auto identity=replay.dataIdentity();auto key="audio/events/"+std::to_string(reinterpret_cast<uintptr_t>(identity.get()));
    if(cacheStore)if(auto timeline=cacheStore->find<AudioTimeline>(key);timeline&&timeline->identity.lock()==identity){
        impl_->timeline=std::move(timeline);impl_->events=impl_->timeline->events;impl_->duration=impl_->timeline->duration;
        if(!impl_->events.empty())impl_->resources=index?std::make_shared<AudioResources>(std::move(index),cacheStore,cache):std::make_shared<AudioResources>(client,cache,true,cacheStore);impl_->indexOggCalls();replay.rewind();return;
    }
    auto timeline=std::make_shared<AudioTimeline>();timeline->identity=identity;
    std::unordered_map<uint32_t,std::vector<AudioEvent>> commands;
    for(const auto& [id,command]:replay.dictionary){
        if(cancelled&&cancelled())throw Error("Audio preparation cancelled");
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
    replay.rewind();Scene scene;uint64_t ordinal=0;
    try{while(replay.next(scene)){
        if(cancelled&&cancelled())throw Error("Audio preparation cancelled");
        impl_->duration=std::max<int64_t>(impl_->duration,scene.timestamp);
        size_t references=0;
        for(auto id:scene.ids){if((++references&1023)==0&&cancelled&&cancelled())throw Error("Audio preparation cancelled");if(auto found=commands.find(id);found!=commands.end())for(auto event:found->second){event.sample=sampleAt(scene.timestamp);event.ordinal=ordinal;timeline->events.push_back(std::move(event));}}++ordinal;
    }}catch(...){replay.rewind();throw;}
    replay.rewind();
    std::stable_sort(timeline->events.begin(),timeline->events.end(),[](const AudioEvent& a,const AudioEvent& b){return a.sample<b.sample;});timeline->duration=impl_->duration;
    if(cancelled&&cancelled())throw Error("Audio preparation cancelled");
    if(cacheStore){size_t bytes=sizeof(AudioTimeline)+timeline->events.capacity()*sizeof(AudioEvent);for(const auto& event:timeline->events)bytes+=event.tag.capacity();cacheStore->insert<AudioTimeline>(key,timeline,bytes);}
    impl_->timeline=std::move(timeline);impl_->events=impl_->timeline->events;
    if(!impl_->events.empty())impl_->resources=index?std::make_shared<AudioResources>(std::move(index),cacheStore,cache):std::make_shared<AudioResources>(client,cache,true,cacheStore);
    impl_->indexOggCalls();
}
AudioTrack::AudioTrack(std::shared_ptr<const AudioClip> clip):impl_(std::make_unique<Impl>()){
    if(!clip||clip->sampleRate!=SampleRate||clip->channels!=2)throw Error("Audition requires a decoded 48 kHz stereo clip");
    impl_->singleClip=std::move(clip);impl_->duration=int64_t((impl_->singleClip->frames()*1000+SampleRate-1)/SampleRate);
}
AudioTrack::~AudioTrack()=default;
bool AudioTrack::hasEvents()const{return !impl_->events.empty()||bool(impl_->singleClip);}
uint64_t AudioTrack::eventCount()const{return impl_->events.size();}
uint64_t AudioTrack::missingResources()const{return impl_->resources?impl_->resources->missing():0;}
int64_t AudioTrack::positionMilliseconds()const{return impl_->position.load()*1000/SampleRate;}
int64_t AudioTrack::durationMilliseconds()const{return impl_->duration;}
std::vector<std::string> AudioTrack::diagnostics()const{auto notes=impl_->resources?impl_->resources->diagnostics():std::vector<std::string>{};notes.insert(notes.end(),impl_->notes.begin(),impl_->notes.end());return notes;}
const void* AudioTrack::eventIdentity()const{return impl_->timeline.get();}
uint64_t AudioTrack::decoderProcesses()const{return impl_->resources?impl_->resources->decoderProcesses():0;}
std::vector<std::string> AudioTrack::oggPaths(std::optional<uint64_t> ordinal)const{
    if(!ordinal)return impl_->allOggs;auto found=impl_->recordOggs.find(*ordinal);return found==impl_->recordOggs.end()?std::vector<std::string>{}:found->second;
}
std::vector<std::string> AudioTrack::activeOggs()const{
    std::vector<std::string> paths;auto at=impl_->position.load();
    for(const auto& voice:impl_->voices)if(voice.start<=at&&!impl_->stopped(voice,at)){
        auto path=oggPath(voice.definition->file);if(!path.empty())paths.push_back(std::move(path));
    }
    std::sort(paths.begin(),paths.end());paths.erase(std::unique(paths.begin(),paths.end()),paths.end());return paths;
}
void AudioTrack::setHiddenOggs(std::unordered_set<std::string> paths){impl_->hiddenOggs=hiddenOggPaths(std::move(paths));}
std::shared_ptr<const AudioClip> AudioTrack::oggClip(const std::string& path,const std::function<bool()>& cancelled)const{
    if(cancelled&&cancelled())return {};auto found=impl_->oggTags.find(oggPath(path));return found==impl_->oggTags.end()?std::shared_ptr<const AudioClip>{}:impl_->resources->clip(found->second,cancelled);
}
void AudioTrack::prepare(int64_t milliseconds,int64_t horizon,const std::function<bool()>& cancelled){
    auto first=sampleAt(milliseconds),last=sampleAt(milliseconds+std::max<int64_t>(0,horizon));
    std::vector<std::string> tags;std::unordered_map<std::string,bool> seen;
    auto begin=std::lower_bound(impl_->events.begin(),impl_->events.end(),first,[](const AudioEvent& event,int64_t at){return event.sample<at;});
    for(auto it=begin;it!=impl_->events.end()&&it->sample<=last;++it){const auto& event=*it;if(event.opcode!=6)continue;
        auto d=impl_->resources->definition(event.tag);if(d&&!d->playable)continue;
        if(seen.emplace(event.tag,true).second)tags.push_back(event.tag);
    }
    auto stopped=[&]{return (cancelled&&cancelled())||(impl_->cancelled&&impl_->cancelled());};
    // Bound optional in-flight retention to one batch window rather than
    // pinning every decoded future clip until a whole recording is prepared.
    for(size_t start=0;start<tags.size();start+=32){if(stopped())return;auto count=std::min<size_t>(32,tags.size()-start);std::vector<std::string> window(tags.begin()+start,tags.begin()+start+count);auto pinned=impl_->resources->prepareClips(window,stopped);pinned.clear();impl_->resources->trimCache();}
}
bool AudioTrack::prepareRender(int64_t horizon,const std::function<bool()>& cancelled){
    if(!impl_->resources)return true;auto last=impl_->position.load()+sampleAt(horizon);std::vector<std::string> tags;std::unordered_set<std::string> seen;
    auto add=[&](const AudioEvent& event){if(event.opcode==6&&seen.insert(event.tag).second)tags.push_back(event.tag);};
    for(size_t n=impl_->cursor;n<impl_->events.size()&&impl_->events[n].sample<=last;n++)add(impl_->events[n]);
    for(const auto& event:impl_->pending)if(event.sample<=last)add(event);
    for(const auto& voice:impl_->voices)add(voice.event);
    std::atomic<size_t> next=0;std::vector<std::future<void>> jobs;std::mutex pinMutex;std::unordered_map<std::string,std::shared_ptr<const AudioClip>> clips;
    for(size_t n=0;n<std::min<size_t>(4,tags.size());n++)jobs.push_back(std::async(std::launch::async,[&]{for(size_t k;(k=next++)<tags.size();){if(cancelled&&cancelled())return;auto clip=impl_->resources->clip(tags[k],cancelled?cancelled:impl_->cancelled);std::lock_guard lock(pinMutex);clips.emplace(tags[k],std::move(clip));}}));
    for(auto& job:jobs)job.get();if(cancelled&&cancelled())return false;impl_->preparedClips=std::move(clips);impl_->resources->trimCache();return true;
}
bool AudioTrack::renderPrepared(std::span<float> output){return renderPreparedActivity(output,nullptr);}
bool AudioTrack::renderPreparedActivity(std::span<float> output,std::vector<OggRange>* activity){
    if(!impl_->resources){renderActivity(output,activity);return true;}auto last=impl_->position.load()+int64_t(output.size()/2);auto clips=impl_->preparedClips;
    for(const auto& voice:impl_->voices)clips.try_emplace(voice.event.tag,voice.clip);
    auto add=[&](const AudioEvent& event){if(event.opcode!=6||clips.contains(event.tag))return true;auto found=impl_->resources->prepared(event.tag);if(!found.ready)return false;clips.emplace(event.tag,std::move(found.clip));return true;};
    for(size_t n=impl_->cursor;n<impl_->events.size()&&impl_->events[n].sample<=last;n++)if(!add(impl_->events[n]))return false;
    for(const auto& event:impl_->pending)if(event.sample<=last&&!add(event))return false;
    impl_->preparedClips=std::move(clips);impl_->preparedOnly=true;struct Reset{Impl& impl;~Reset(){impl.preparedOnly=false;impl.preparedClips.clear();}} reset{*impl_};renderActivity(output,activity);return true;
}
bool AudioTrack::seek(int64_t milliseconds,const std::function<bool()>& cancelled){
    impl_->seekCancelled=[&]{return (cancelled&&cancelled())||(impl_->cancelled&&impl_->cancelled());};
    struct Clear {std::function<bool()>& value;~Clear(){value={};}} clear{impl_->seekCancelled};
    try{
        impl_->checkSeekCancelled();auto target=sampleAt(milliseconds);
        if(impl_->singleClip){impl_->position=std::max<int64_t>(0,target);return true;}
        // Acquiring clip bytes has no interpreter side effects. Prepare the
        // conservative past request set in parallel, then run the same exact
        // stop/rank/delay/loop interpreter below. Unreached delayed requests
        // cannot be needed at this target. Stop-filtered requests may be warmed
        // speculatively; they are never emitted or otherwise applied here.
        std::vector<std::string> tags;std::unordered_set<std::string> seen;
        for(size_t n=0;n<impl_->events.size()&&impl_->events[n].sample<=target;n++){
            if((n&1023)==0)impl_->checkSeekCancelled();const auto& event=impl_->events[n];if(event.opcode!=6)continue;
            if(event.words[2]>0&&event.sample+sampleAt(event.words[2])>target)continue;
            auto definition=impl_->resources->definition(event.tag);if(!definition||!definition->playable)continue;
            if(seen.insert(event.tag).second)tags.push_back(event.tag);
        }
        std::atomic<bool> aborted=false;std::mutex cancellationMutex;
        auto preparationCancelled=[&]{std::lock_guard lock(cancellationMutex);if(aborted)return true;if(impl_->seekCancelled&&impl_->seekCancelled()){aborted=true;return true;}return false;};
        auto pinned=impl_->resources?impl_->resources->prepareClips(tags,preparationCancelled):std::vector<std::shared_ptr<const AudioClip>>{};if(aborted)throw AudioSeekCancelled{};
        impl_->checkSeekCancelled();impl_->trackedEndBoundary.reset();impl_->cursor=0;impl_->position=0;impl_->voices.clear();impl_->pending.clear();impl_->ranks.clear();impl_->loopCounters.clear();impl_->advance(target);
        impl_->checkSeekCancelled();pinned.clear();if(impl_->resources)impl_->resources->trimCache();return true;
    }catch(const AudioSeekCancelled&){if(impl_->resources)impl_->resources->trimCache();return false;}
}
void AudioTrack::render(std::span<float> output){renderActivity(output,nullptr);}
void AudioTrack::renderActivity(std::span<float> output,std::vector<OggRange>* activity){
    if(output.size()%2)throw Error("Audio buffer must contain stereo frames");
    if(activity)activity->clear();
    std::fill(output.begin(),output.end(),0.f);size_t completed=0,total=output.size()/2;
    if(impl_->singleClip){
        auto start=uint64_t(impl_->position.load()),length=impl_->singleClip->frames();
        if(start<length){auto count=size_t(std::min<uint64_t>(total,length-start));std::copy_n(impl_->singleClip->samples.begin()+size_t(start)*2,count*2,output.begin());}
        impl_->position+=int64_t(total);if(activity){activity->push_back({int64_t(start),impl_->position.load(),{}});activity->push_back({impl_->position.load(),impl_->position.load(),{}});}return;
    }
    while(completed<total){
        int64_t start=impl_->position.load();impl_->renderBoundary(start);
        auto count=size_t(std::max<int64_t>(1,impl_->nextBoundary(start+int64_t(total-completed))-start));
        count=std::min(count,total-completed);
        if(activity){
            auto paths=activeOggs();auto last=start+int64_t(count);
            if(!activity->empty()&&activity->back().lastSample==start&&activity->back().paths==paths)activity->back().lastSample=last;
            else activity->push_back({start,last,std::move(paths)});
        }
        for(const auto& voice:impl_->voices){
            if(impl_->hiddenOggs.contains(oggPath(voice.definition->file)))continue;
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
    if(activity){
        // Prepared acquisition includes events at the buffer end. Capture that
        // exact boundary for a device stopped precisely at the submitted tail.
        auto at=impl_->position.load();impl_->renderBoundary(at);impl_->trackedEndBoundary=at;activity->push_back({at,at,activeOggs()});
    }
    for(float& value:output)value=std::clamp(value,-1.f,1.f);
}

struct AudioPlayer::Impl {
    using Clock=std::chrono::steady_clock;
    std::unique_ptr<AudioTrack> track;
    std::thread thread;
    mutable std::mutex mutex;
    std::condition_variable condition;
    bool quit=false,ready=false,playing=false,seekRequested=false,forcedBuffering=false,remainingEnabled=true;
    bool buffering=false,clockRunning=false;
    int64_t requested=0;
    uint64_t requestedSerial=0;
    std::function<bool()> seekCancellation;
    std::atomic<uint64_t> appliedSerial=0,pcmSerial=0,rejectedSerial=0,deviceFrames=0,underruns=0,bufferingEvents=0,prefetchEpoch=0,remainingEpoch=0;
    float volume=1,playbackRate=1;
    bool muted=false;
    std::unordered_set<std::string> hiddenOggs;
    std::atomic<bool> available=false;
    std::atomic<int64_t> position=0,bufferedMilliseconds=0;
    std::string failure;
    uint64_t oggRangesSerial=0;
    std::deque<AudioTrack::OggRange> oggRanges;
    OggActivity oggSnapshot;
    // Source PCM stays valid until the corresponding voice is destroyed.
    std::array<std::array<float,960>,4> buffers{};
    void run(){
        auto co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);IXAudio2* engine=nullptr;IXAudio2MasteringVoice* master=nullptr;IXAudio2SourceVoice* voice=nullptr;
        std::future<void> prefetch,remaining;
        auto close=[&]{if(voice)voice->DestroyVoice();if(master)master->DestroyVoice();if(engine)engine->Release();if(SUCCEEDED(co))CoUninitialize();};
        auto cancelPrefetch=[&]{++prefetchEpoch;++remainingEpoch;if(prefetch.valid())try{prefetch.get();}catch(...){}if(remaining.valid())try{remaining.get();}catch(...){};};
        try{
            if(FAILED(XAudio2Create(&engine,0,XAUDIO2_DEFAULT_PROCESSOR))||FAILED(engine->CreateMasteringVoice(&master)))throw Error("Default audio output device is unavailable");
            WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;format.nChannels=2;format.nSamplesPerSec=AudioTrack::SampleRate;
            format.wBitsPerSample=32;format.nBlockAlign=8;format.nAvgBytesPerSec=format.nSamplesPerSec*8;
            auto create=[&]{if(FAILED(engine->CreateSourceVoice(&voice,&format,0,4,nullptr)))throw Error("Cannot create REP audio output voice");};create();
            {std::lock_guard lock(mutex);available=true;ready=true;condition.notify_all();}
            size_t bufferIndex=0;bool active=false,hadSteadyQueue=false,underrunEpisode=false,windowPrepared=false,remainingComplete=false;int64_t base=0,lastPrefetch=-1,windowEnd=0;uint64_t currentTicket=0,remainingTicket=0,sourceSerial=0;
            auto readClock=[&](bool wasActive=false){XAUDIO2_VOICE_STATE state{};voice->GetState(&state);
                if(state.BuffersQueued>=3)hadSteadyQueue=true;
                if((active||wasActive)&&hadSteadyQueue&&state.SamplesPlayed>0&&state.BuffersQueued==0){if(!underrunEpisode)++underruns;underrunEpisode=true;}else if(state.BuffersQueued>0)underrunEpisode=false;
                auto sample=sampleAt(base)+int64_t(state.SamplesPlayed);
                deviceFrames=state.SamplesPlayed;position=sample*1000/AudioTrack::SampleRate;bufferedMilliseconds=std::max<int64_t>(0,track->positionMilliseconds()-position.load());
                {std::lock_guard lock(mutex);if(sourceSerial==requestedSerial&&oggRangesSerial==sourceSerial){
                    // Keep only the current submitted interval and its queued
                    // successors, including a single exact end-point snapshot.
                    while(oggRanges.size()>1&&oggRanges.front().lastSample<=sample)oggRanges.pop_front();
                    oggSnapshot={sourceSerial,sample,position.load(),false,{}};
                    for(const auto& range:oggRanges)if(range.firstSample<=sample&&(sample<range.lastSample||(range.firstSample==range.lastSample&&sample==range.firstSample))){oggSnapshot.ready=true;oggSnapshot.paths=range.paths;break;}
                }}return state;};
            auto freeze=[&](bool){bool wasActive=active;if(active){voice->Stop();active=false;}readClock(wasActive);std::lock_guard lock(mutex);clockRunning=false;if(wasActive&&!buffering)++bufferingEvents;buffering=true;condition.notify_all();};
            while(true){
                bool wanted,seek,forced;int64_t target;float gain,rate;uint64_t serial;std::function<bool()> cancellation;std::unordered_set<std::string> selection;
                {std::unique_lock lock(mutex);if(quit)break;wanted=playing;seek=std::exchange(seekRequested,false);forced=forcedBuffering;target=requested;serial=requestedSerial;cancellation=seekCancellation;gain=muted?0.f:volume;rate=playbackRate;if(seek)selection=hiddenOggs;}
                auto cancelled=[&]{{std::lock_guard lock(mutex);if(quit||requestedSerial!=serial)return true;}return cancellation&&cancellation();};
                auto reject=[&]{freeze(false);cancelPrefetch();rejectedSerial=serial;std::lock_guard lock(mutex);if(requestedSerial==serial){playing=false;oggRanges.clear();oggSnapshot.ready=false;oggSnapshot.paths.clear();}};
                if(serial&&rejectedSerial.load()==serial){std::unique_lock lock(mutex);condition.wait_for(lock,std::chrono::milliseconds(10),[&]{return quit||seekRequested||requestedSerial!=serial;});continue;}
                voice->SetVolume(gain);voice->SetFrequencyRatio(rate);
                if(forced)freeze(false);
                if(seek){
                    // Stop and recreate before expensive state work. No device
                    // clock runs while a selected target is being prepared.
                    freeze(false);cancelPrefetch();base=position.load();voice->DestroyVoice();voice=nullptr;create();voice->SetVolume(gain);voice->SetFrequencyRatio(rate);
                    bufferIndex=0;lastPrefetch=-1;deviceFrames=0;bufferedMilliseconds=0;hadSteadyQueue=false;underrunEpisode=false;windowPrepared=remainingComplete=false;
                    track->setHiddenOggs(std::move(selection));if(!track->seek(target,cancelled)||cancelled()){reject();continue;}
                    base=target;sourceSerial=serial;
                    {std::lock_guard lock(mutex);if(quit||requestedSerial!=serial)continue;position=target;appliedSerial=serial;oggRanges.clear();oggRangesSerial=serial;oggSnapshot={serial,sampleAt(target),target,false,{}};}
                }
                if(cancelled()){reject();continue;}
                if(!serial){std::unique_lock lock(mutex);condition.wait_for(lock,std::chrono::milliseconds(10));continue;}
                auto state=readClock();
                bool needInitial=pcmSerial.load()<serial;
                if(needInitial||(wanted&&!forced)){
                    while(state.BuffersQueued<3&&!cancelled()){
                        auto& buffer=buffers[bufferIndex%buffers.size()];
                        std::vector<AudioTrack::OggRange> activity;
                        if(!track->renderPreparedActivity(buffer,&activity)){
                            freeze(active);cancelPrefetch();
                            if(!track->prepareRender(10,cancelled)||cancelled())break;
                            if(!track->renderPreparedActivity(buffer,&activity))throw Error("Prepared REP audio buffer lost a required clip");
                        }
                        if(cancelled())break;
                        XAUDIO2_BUFFER submission{};submission.AudioBytes=DWORD(buffer.size()*sizeof(float));submission.pAudioData=reinterpret_cast<BYTE*>(buffer.data());
                        if(FAILED(voice->SubmitSourceBuffer(&submission)))throw Error("REP audio output rejected PCM buffer");
                        {std::lock_guard lock(mutex);if(requestedSerial==serial&&oggRangesSerial==serial){for(auto& range:activity)oggRanges.push_back(std::move(range));}}
                        ++bufferIndex;state=readClock();
                    }
                    if(cancelled()){reject();continue;}
                    readClock();
                    {std::lock_guard lock(mutex);if(quit||requestedSerial!=serial)continue;if(state.BuffersQueued>=3){pcmSerial=serial;condition.notify_all();}}
                }
                {std::lock_guard lock(mutex);wanted=playing;forced=forcedBuffering;if(requestedSerial!=serial)continue;}
                if(wanted&&!forced&&pcmSerial.load()>=serial){
                    {std::lock_guard lock(mutex);
                        if(!quit&&requestedSerial==serial&&playing&&!forcedBuffering&&pcmSerial.load()>=serial&&rejectedSerial.load()!=serial){
                            if(!active){voice->SetVolume(muted?0.f:volume);voice->SetFrequencyRatio(playbackRate);if(FAILED(voice->Start()))throw Error("REP audio output cannot start");active=true;}
                            clockRunning=true;buffering=false;condition.notify_all();
                        }
                    }
                }else{
                    if(active){voice->Stop();active=false;readClock();}
                    std::lock_guard lock(mutex);clockRunning=false;buffering=forcedBuffering;
                }
                if(prefetch.valid()&&prefetch.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){bool success=true;try{prefetch.get();}catch(...){success=false;}windowPrepared=success&&currentTicket==prefetchEpoch.load();}
                if(remaining.valid()&&remaining.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){bool success=true;try{remaining.get();}catch(...){success=false;}remainingComplete|=success&&remainingTicket==remainingEpoch.load();}
                if(windowPrepared&&currentTicket!=prefetchEpoch.load())windowPrepared=false;
                bool allowRemaining;{std::lock_guard lock(mutex);forced=forcedBuffering;allowRemaining=remainingEnabled;}
                if(!forced&&pcmSerial.load()>=serial&&!cancelled()){
                    auto at=track->positionMilliseconds();
                    if(!prefetch.valid()&&(lastPrefetch<0||at-lastPrefetch>=500||!windowPrepared)){
                        // Invalidate low work without waiting on an active
                        // device queue. The new rolling window takes priority;
                        // low workers notice cancellation before publication.
                        ++remainingEpoch;windowPrepared=false;lastPrefetch=at;windowEnd=at+2000;currentTicket=++prefetchEpoch;auto ticket=currentTicket;
                        prefetch=std::async(std::launch::async,[&,ticket,serial,at]{track->prepare(at,2000,[&]{std::lock_guard lock(mutex);return quit||requestedSerial!=serial||prefetchEpoch.load()!=ticket||forcedBuffering;});});
                    }
                    if(allowRemaining&&windowPrepared&&!remainingComplete&&!remaining.valid()&&windowEnd<track->durationMilliseconds()){
                        auto from=windowEnd,horizon=track->durationMilliseconds()-from;remainingTicket=++remainingEpoch;auto ticket=remainingTicket;
                        remaining=std::async(std::launch::async,[&,ticket,serial,from,horizon]{track->prepare(from,horizon,[&]{std::lock_guard lock(mutex);return quit||requestedSerial!=serial||remainingEpoch.load()!=ticket||forcedBuffering||!remainingEnabled;});});
                    }
                }
                std::unique_lock lock(mutex);condition.wait_for(lock,std::chrono::milliseconds(active?2:10),[&]{return quit||seekRequested||forcedBuffering!=forced||playing!=wanted;});
            }
        }catch(const std::exception& e){std::lock_guard lock(mutex);failure=e.what();available=false;ready=true;clockRunning=false;buffering=false;oggRanges.clear();oggSnapshot.ready=false;oggSnapshot.paths.clear();condition.notify_all();}
        cancelPrefetch();close();
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
void AudioPlayer::stop(){seek(0,false);}
uint64_t AudioPlayer::seek(int64_t milliseconds,bool playing,const std::function<bool()>& cancelled){std::lock_guard lock(impl_->mutex);impl_->requested=std::max<int64_t>(0,milliseconds);impl_->playing=playing;impl_->seekCancellation=cancelled;impl_->seekRequested=true;auto serial=++impl_->requestedSerial;impl_->oggRanges.clear();impl_->oggSnapshot={serial,sampleAt(impl_->requested),impl_->requested,false,{}};impl_->condition.notify_all();return serial;}
uint64_t AudioPlayer::appliedSeekSerial()const{return impl_->appliedSerial;}
uint64_t AudioPlayer::pcmReadySerial()const{return impl_->pcmSerial;}
AudioPlayer::Statistics AudioPlayer::stats()const{std::lock_guard lock(impl_->mutex);return {impl_->appliedSerial.load(),impl_->pcmSerial.load(),impl_->deviceFrames.load(),impl_->underruns.load(),impl_->bufferedMilliseconds.load(),impl_->buffering,impl_->forcedBuffering,impl_->clockRunning,impl_->bufferingEvents.load()};}
AudioPlayer::OggActivity AudioPlayer::oggActivity()const{
    std::lock_guard lock(impl_->mutex);auto result=impl_->oggSnapshot;
    result.ready=result.ready&&impl_->available&&result.seekSerial==impl_->requestedSerial&&impl_->pcmSerial.load()>=result.seekSerial&&impl_->rejectedSerial.load()!=result.seekSerial;
    if(!result.ready)result.paths.clear();return result;
}
void AudioPlayer::pauseBuffering(bool paused){std::lock_guard lock(impl_->mutex);impl_->forcedBuffering=paused;if(paused){++impl_->prefetchEpoch;++impl_->remainingEpoch;if(!impl_->clockRunning)impl_->buffering=true;}impl_->condition.notify_all();}
void AudioPlayer::setRemainingPrefetchEnabled(bool enabled){std::lock_guard lock(impl_->mutex);impl_->remainingEnabled=enabled;if(!enabled)++impl_->remainingEpoch;impl_->condition.notify_all();}
bool AudioPlayer::resumePrepared(uint64_t serial){std::lock_guard lock(impl_->mutex);if(impl_->requestedSerial!=serial||impl_->rejectedSerial.load()==serial||impl_->appliedSerial.load()<serial||impl_->pcmSerial.load()<serial)return false;impl_->playing=true;impl_->condition.notify_all();return true;}
void AudioPlayer::volume(float value,bool muted){std::lock_guard lock(impl_->mutex);impl_->volume=std::clamp(value,0.f,1.f);impl_->muted=muted;impl_->condition.notify_all();}
void AudioPlayer::setPlaybackRate(double rate){
    if(!std::isfinite(rate)||rate<.25||rate>4)throw Error("Playback speed must be between 0.25 and 4");
    std::lock_guard lock(impl_->mutex);impl_->playbackRate=float(rate);impl_->condition.notify_all();
}
std::vector<std::string> AudioPlayer::oggPaths(std::optional<uint64_t> ordinal)const{return impl_->track->oggPaths(ordinal);}
void AudioPlayer::setHiddenOggs(std::unordered_set<std::string> paths){std::lock_guard lock(impl_->mutex);impl_->hiddenOggs=hiddenOggPaths(std::move(paths));}
std::shared_ptr<const AudioClip> AudioPlayer::oggClip(const std::string& path,const std::function<bool()>& cancelled)const{return impl_->track->oggClip(path,cancelled);}
bool AudioPlayer::available()const{return impl_->available;}
std::string AudioPlayer::error()const{std::lock_guard lock(impl_->mutex);return impl_->failure;}
std::vector<std::string> AudioPlayer::diagnostics()const{return impl_->track->diagnostics();}
uint64_t AudioPlayer::missingResources()const{return impl_->track->missingResources();}
uint64_t AudioPlayer::eventCount()const{return impl_->track->eventCount();}
int64_t AudioPlayer::positionMilliseconds()const{return impl_->position;}
int64_t AudioPlayer::durationMilliseconds()const{return impl_->track->durationMilliseconds();}
}
