#include "ui_playback.hpp"
#include "audio.hpp"
#include "audio_resources.hpp"
#include "retained_cache.hpp"
#include <algorithm>
#include <fstream>
#include <future>
#include <utility>
#include <cmath>

namespace rep::ui {
namespace {
struct CachedInspection {
    std::weak_ptr<const void> identity;
    ReplayInspection value;
};
struct DemandCancellationScope {
    Assets& assets;std::function<bool()>& current;std::function<bool()> previous;
    DemandCancellationScope(Assets& value,std::function<bool()>& slot,std::function<bool()> next):assets(value),current(slot),previous(slot){current=std::move(next);assets.setDemandCancellation(current);}
    ~DemandCancellationScope(){current=std::move(previous);assets.setDemandCancellation(current);}
};
static std::string inspectionKey(const Replay& replay){return "inspection:"+std::to_string(reinterpret_cast<uintptr_t>(replay.dataIdentity().get()));}
// One worker owns at most one active scan and the latest queued scan. A new
// picture never joins an obsolete scan; each scan owns its independent cursor.
class ReplayInspector {
    struct Request {std::unique_ptr<Replay> replay;uint64_t generation,ticket;std::function<bool()> cancelled;};
    struct Result {uint64_t generation;ReplayInspection inspection;std::exception_ptr error;};
    std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<Request> pending_;
    std::optional<Result> result_;
    std::atomic<bool> quitting_=false;
    std::atomic<uint64_t> ticket_=0;
    std::thread thread_;
    std::shared_ptr<RetainedCache> cache_;
    void run(){
        while(!quitting_){
            std::optional<Request> request;
            {std::unique_lock lock(mutex_);condition_.wait(lock,[&]{return quitting_||pending_.has_value();});if(quitting_)break;request=std::move(pending_);pending_.reset();}
            auto cancelled=[&]{return quitting_||ticket_.load()!=request->ticket||(request->cancelled&&request->cancelled());};
            Result result{request->generation,{},{}};
            try{if(!cancelled())result.inspection=inspectReplayImages(*request->replay,cancelled);}catch(...){result.error=std::current_exception();}
            if(!cancelled()&&!result.error&&cache_){
                auto value=std::make_shared<CachedInspection>();value->identity=request->replay->dataIdentity();value->value=result.inspection;
                size_t bytes=sizeof(CachedInspection)+value->value.images.capacity()*sizeof(ImgCall);
                for(const auto& image:value->value.images)bytes+=image.path.capacity()+image.role.capacity();
                cache_->insert<CachedInspection>(inspectionKey(*request->replay),value,bytes);
            }
            std::lock_guard lock(mutex_);if(!cancelled())result_=std::move(result);
        }
    }
public:
    explicit ReplayInspector(std::shared_ptr<RetainedCache> cache):cache_(std::move(cache)){thread_=std::thread([this]{run();});}
    ~ReplayInspector(){quitting_=true;condition_.notify_all();if(thread_.joinable())thread_.join();}
    void cancel(){std::lock_guard lock(mutex_);ticket_++;pending_.reset();result_.reset();}
    void submit(const Replay& replay,uint64_t generation,std::function<bool()> cancelled){
        if(cache_)if(auto value=cache_->find<CachedInspection>(inspectionKey(replay));value&&value->identity.lock()==replay.dataIdentity()){
            std::lock_guard lock(mutex_);++ticket_;pending_.reset();result_=Result{generation,value->value,{}};return;
        }
        auto cursor=std::make_unique<Replay>(replay);
        std::lock_guard lock(mutex_);auto ticket=++ticket_;result_.reset();pending_=Request{std::move(cursor),generation,ticket,std::move(cancelled)};condition_.notify_all();
    }
    std::optional<ReplayInspection> take(uint64_t generation){
        std::lock_guard lock(mutex_);if(!result_)return {};auto result=std::move(*result_);result_.reset();if(result.generation!=generation)return {};
        if(result.error)std::rethrow_exception(result.error);return std::move(result.inspection);
    }
};
// This cursor only schedules future inputs. It never executes instructions or
// contributes camera/context/random/movie state to interactive selection.
class FontWarmup {
    struct Request {std::unique_ptr<Replay> replay;std::filesystem::path client;std::vector<uint32_t> ids;uint64_t generation,ticket;std::function<bool()> cancelled;};
    std::shared_ptr<RetainedCache> cache_;
    std::mutex mutex_;std::condition_variable condition_;std::optional<Request> pending_;
    std::atomic<bool> quit_=false;std::atomic<uint64_t> ticket_=0;std::thread thread_;
    void run(){
        std::unique_ptr<Fonts> fonts;uint64_t active=0,seenTicket=ticket_.load();
        while(!quit_){
            std::optional<Request> request;
            {std::unique_lock lock(mutex_);condition_.wait(lock,[&]{return quit_||pending_.has_value()||ticket_!=seenTicket;});if(quit_)break;
                if(ticket_!=seenTicket){fonts.reset();active=0;seenTicket=ticket_.load();}
                if(!pending_)continue;request=std::move(pending_);pending_.reset();}
            auto cancelled=[&]{return quit_||ticket_!=request->ticket||(request->cancelled&&request->cancelled());};
            try{
                if(cancelled()){fonts.reset();active=0;continue;}
                if(!fonts||active!=request->generation){fonts=std::make_unique<Fonts>(request->client/L"Fonts",cache_);active=request->generation;}
                for(auto id:request->ids){if(cancelled())break;if(auto command=request->replay->command(id))
                    for(const auto& i:command->instructions){if(cancelled())break;if(i.opcode==5||i.opcode==48)fonts->prewarmGlyphs(i,*request->replay);}}
            }catch(...){/* A scheduling hint cannot fail the selected scene. */}
            if(cancelled()){fonts.reset();active=0;}
        }
    }
public:
    explicit FontWarmup(std::shared_ptr<RetainedCache> cache):cache_(std::move(cache)),thread_([this]{run();}){}
    ~FontWarmup(){quit_=true;condition_.notify_all();if(thread_.joinable())thread_.join();}
    void cancel(){std::lock_guard lock(mutex_);++ticket_;pending_.reset();condition_.notify_all();}
    void submit(const Replay& replay,const std::filesystem::path& client,std::vector<uint32_t> ids,uint64_t generation,std::function<bool()> cancelled){
        if(ids.empty())return;std::lock_guard lock(mutex_);
        if(pending_&&pending_->generation==generation)pending_->ids.insert(pending_->ids.end(),ids.begin(),ids.end());
        else pending_=Request{std::make_unique<Replay>(replay),client,std::move(ids),generation,ticket_.load(),std::move(cancelled)};
        condition_.notify_all();
    }
};
class ImageReadAhead {
    std::unique_ptr<Replay> cursor_;
    std::unordered_map<uint32_t,int> scheduled_;
    std::unordered_map<uint32_t,int32_t> window_;
    std::unordered_map<uint32_t,Command>::const_iterator dictionary_;
    Scene next_;bool pending_=false,eof_=false;
    Clock::time_point touched_{};
public:
    void reset(const Replay& replay){cursor_=std::make_unique<Replay>(replay);dictionary_=cursor_->dictionary.begin();scheduled_.clear();window_.clear();pending_=eof_=false;touched_={};}
    void cancel(){cursor_.reset();scheduled_.clear();window_.clear();pending_=eof_=false;}
    void advance(Assets& assets,FontWarmup& fonts,const std::filesystem::path& client,int64_t elapsed,uint64_t generation,bool allowDictionary,const std::function<bool()>& cancelled){
        if(!cursor_||cancelled())return;std::vector<Assets::FrameRequest> requests;std::vector<uint32_t> fontCommands;
        auto schedule=[&](uint32_t id,int priority){
            auto [entry,fresh]=scheduled_.try_emplace(id,priority);if(!fresh&&entry->second<=priority)return;entry->second=priority;
            if(auto command=cursor_->command(id)){
                auto values=commandImageRequests(*cursor_,*command,priority);requests.insert(requests.end(),std::make_move_iterator(values.begin()),std::make_move_iterator(values.end()));
                if(fresh&&std::any_of(command->instructions.begin(),command->instructions.end(),[](const auto& i){return i.opcode==5||i.opcode==48;}))fontCommands.push_back(id);
            }
        };
        auto deadline=Clock::now()+std::chrono::milliseconds(2);
        unsigned records=0;
        while(!eof_&&!cancelled()&&records++<32){
            if(!pending_){if(!cursor_->next(next_,cancelled)){eof_=true;break;}pending_=true;}
            if(next_.timestamp>elapsed+2000)break;
            if(next_.timestamp>=elapsed-100)for(auto id:next_.ids){window_[id]=next_.timestamp;schedule(id,1);}
            pending_=false;if(Clock::now()>=deadline)break;
        }
        // The whole dictionary remains a lower-priority hint, spread over
        // iterations so neither collecting it nor decoding it blocks start.
        if(allowDictionary&&assets.prefetchStats().pendingJobs<16&&(eof_||(pending_&&next_.timestamp>elapsed+2000)))for(unsigned n=0;n<8&&dictionary_!=cursor_->dictionary.end()&&!cancelled();++n,++dictionary_)schedule(dictionary_->first,2);
        if(Clock::now()-touched_>=std::chrono::milliseconds(100)){
            for(auto it=window_.begin();it!=window_.end();){
                if(it->second<elapsed-100){it=window_.erase(it);continue;}
                if(auto command=cursor_->command(it->first)){auto values=commandImageRequests(*cursor_,*command,1);requests.insert(requests.end(),std::make_move_iterator(values.begin()),std::make_move_iterator(values.end()));}++it;
            }
            touched_=Clock::now();
        }
        assets.prefetch(requests,generation,cancelled);fonts.submit(*cursor_,client,std::move(fontCommands),generation,cancelled);
    }
};
}
PlaybackController::PlaybackController(HWND canvas,std::filesystem::path root,std::filesystem::path client,bool test,Clock::time_point processStart,ClientProtocolSelection protocol,CanvasSettings settings,bool patchesEnabled)
    :canvas_(canvas),root_(std::move(root)),client_(std::move(client)),protocol_(std::move(protocol)),canvasSettings_(std::move(settings)),test_(test),processStart_(processStart),patchesEnabled_(patchesEnabled) {
    status_.patchesEnabled=patchesEnabled;
    if(!client_.empty()){stopRequested_=true;generation_=1;}
    thread_=std::thread([this]{run();});
}
PlaybackController::~PlaybackController(){quitting_=true;generation_++;condition_.notify_all();if(thread_.joinable())thread_.join();}
void PlaybackController::open(const std::filesystem::path& path){std::lock_guard lock(mutex_);cancelAuditionLocked();if(path.wstring()!=status_.path)hiddenOggs_.clear();status_.hiddenOggs=hiddenOggs_;status_.currentOggs.clear();status_.allOggs.clear();setHighlightedImage({});requested_=path;requestedAt_=Clock::now();stopRequested_=false;navigationRequested_.reset();activeNavigation_.reset();navigationSerial_++;scrubbing_=status_.scrubbing=false;hidden_.clear();imageVisibility_.clearManual();status_.imageVisibility=imageVisibility_;status_.frameCount=0;status_.hiddenImages.clear();status_.currentImages.clear();status_.allImages.clear();status_.phase=Phase::Loading;status_.compatibilityIgnored=false;status_.message.clear();status_.path=path.wstring();generation_++;condition_.notify_all();}
void PlaybackController::replay(){auto s=status();if(s.path.empty())return;std::lock_guard lock(mutex_);cancelAuditionLocked();requested_=s.path;requestedAt_=Clock::now();stopRequested_=false;navigationRequested_.reset();activeNavigation_.reset();navigationSerial_++;scrubbing_=status_.scrubbing=false;status_.phase=Phase::Loading;generation_++;condition_.notify_all();}
void PlaybackController::stop(){std::lock_guard lock(mutex_);cancelAuditionLocked();stopRequested_=true;navigationRequested_.reset();activeNavigation_.reset();navigationSerial_++;scrubbing_=status_.scrubbing=false;generation_++;condition_.notify_all();}
void PlaybackController::togglePause(){
    std::lock_guard lock(mutex_);if(status_.path.empty())return;
    cancelAuditionLocked();
    if(status_.phase==Phase::Seeking){
        if(scrubbing_)scrubResume_=!scrubResume_;
        if(navigationRequested_)navigationRequested_->resume=!navigationRequested_->resume;
        if(activeNavigation_)activeNavigation_->resume=!activeNavigation_->resume;
    }else if(status_.phase==Phase::Paused){
        NavigationRequest request;request.byOrdinal=true;request.ordinal=status_.ordinal;request.time=status_.elapsed;
        request.preserveState=true;request.resumeOnly=true;request.resume=true;request.originalPhase=Phase::Paused;
        queueNavigationLocked(request);
    }else if(status_.phase==Phase::Stopped||status_.phase==Phase::Ended){
        requested_=status_.path;requestedAt_=Clock::now();stopRequested_=false;status_.phase=Phase::Loading;generation_++;
    }else toggleRequested_=true;
    condition_.notify_all();
}
void PlaybackController::cancelAuditionLocked(){
    auditionSerial_++;auditionRequested_=std::string{};status_.auditionPath.clear();status_.auditionMessage.clear();
}
void PlaybackController::queueNavigationLocked(NavigationRequest request,bool stopAudition){
    if(stopAudition)cancelAuditionLocked();
    request.serial=++navigationSerial_;navigationRequested_=request;status_.phase=Phase::Seeking;
    status_.navigationSerial=request.serial;status_.navigationTarget=request.time;status_.navigationProgressOrdinal=0;
    condition_.notify_all();
}
void PlaybackController::queueRefreshLocked(){
    if(status_.path.empty()||status_.phase==Phase::Loading||status_.phase==Phase::Error)return;
    NavigationRequest request;
    if(navigationRequested_)request=*navigationRequested_;
    else if(activeNavigation_)request=*activeNavigation_;
    else {request.byOrdinal=true;request.ordinal=status_.ordinal;request.time=status_.elapsed;request.preserveState=true;request.redraw=true;request.originalPhase=status_.phase;request.resume=status_.phase==Phase::Playing;}
    if(request.resumeOnly){request.resumeOnly=false;request.redraw=true;}queueNavigationLocked(request);
}
void PlaybackController::stepFrame(int direction,bool repeated){
    if(!direction)return;std::lock_guard lock(mutex_);
    if(status_.path.empty()||status_.phase==Phase::Loading||status_.phase==Phase::Error||scrubbing_)return;
    if(repeated&&(status_.phase==Phase::Seeking||navigationRequested_||activeNavigation_))return;
    uint64_t ordinal=status_.ordinal;
    if(navigationRequested_&&navigationRequested_->byOrdinal)ordinal=navigationRequested_->ordinal;
    else if(activeNavigation_&&activeNavigation_->byOrdinal)ordinal=activeNavigation_->ordinal;
    if(direction<0){if(ordinal)ordinal--;}
    else if(!status_.frameCount||ordinal+1<status_.frameCount)ordinal++;
    NavigationRequest request;request.byOrdinal=true;request.ordinal=ordinal;request.time=status_.elapsed;queueNavigationLocked(request);
}
void PlaybackController::beginScrub(){
    std::lock_guard lock(mutex_);if(status_.path.empty()||status_.phase==Phase::Loading||status_.phase==Phase::Error||scrubbing_)return;
    cancelAuditionLocked();
    scrubOriginalPhase_=status_.phase==Phase::Seeking?Phase::Paused:status_.phase;
    scrubResume_=status_.phase==Phase::Playing||(navigationRequested_&&navigationRequested_->resume)||(activeNavigation_&&activeNavigation_->resume);
    navigationRequested_.reset();activeNavigation_.reset();status_.navigationSerial=++navigationSerial_;status_.navigationTarget=status_.elapsed;scrubbing_=true;scrubChanged_=true;status_.phase=Phase::Seeking;condition_.notify_all();
}
void PlaybackController::endScrub(std::optional<int64_t> target,bool endpointLast){
    std::lock_guard lock(mutex_);if(!scrubbing_)return;scrubbing_=false;scrubChanged_=true;
    NavigationRequest request;request.resume=scrubResume_;
    if(target){request.time=std::max<int64_t>(0,*target);request.last=endpointLast;}
    else {request.byOrdinal=true;request.ordinal=status_.ordinal;request.time=status_.elapsed;request.preserveState=true;request.resumeOnly=true;request.originalPhase=scrubOriginalPhase_;}
    queueNavigationLocked(request);
}
void PlaybackController::seekTime(int64_t target,bool endpointLast){
    std::lock_guard lock(mutex_);if(status_.path.empty()||status_.phase==Phase::Loading||status_.phase==Phase::Error)return;
    NavigationRequest request;request.time=std::max<int64_t>(0,target);request.last=endpointLast;
    request.resume=status_.phase==Phase::Playing||(navigationRequested_&&navigationRequested_->resume)||(activeNavigation_&&activeNavigation_->resume);
    queueNavigationLocked(request);
}
void PlaybackController::configureClient(const std::filesystem::path& client){std::lock_guard lock(mutex_);cancelAuditionLocked();hiddenOggs_.clear();setHighlightedImage({});client_=client;requested_.clear();hidden_.clear();imageVisibility_.clearManual();navigationRequested_.reset();activeNavigation_.reset();navigationSerial_++;scrubbing_=false;stopRequested_=true;toggleRequested_=filterRequested_=captureRequested_=false;refreshImagesRequested_=true;imageIndex_.reset();audioIndex_.reset();status_=PlayerStatus{};status_.playbackRate=playbackRate_;status_.client=client_;status_.audioVolume=audioVolume_;status_.audioMuted=audioMuted_;status_.negativeImgOffsetsEnabled=negativeImgOffsetsEnabled_;status_.patchesEnabled=patchesEnabled_;status_.imageVisibility=imageVisibility_;generation_++;condition_.notify_all();}
void PlaybackController::refreshImages(std::optional<bool> patchesEnabled){std::lock_guard lock(mutex_);if(client_.empty())return;cancelAuditionLocked();if(patchesEnabled)patchesEnabled_=*patchesEnabled;requested_=status_.path;requestedAt_=Clock::now();stopRequested_=requested_.empty();navigationRequested_.reset();activeNavigation_.reset();navigationSerial_++;scrubbing_=status_.scrubbing=false;refreshImagesRequested_=true;imageIndex_.reset();audioIndex_.reset();status_.phase=Phase::Loading;status_.frameCount=0;status_.currentImages.clear();status_.allImages.clear();status_.currentOggs.clear();status_.allOggs.clear();status_.message.clear();generation_++;condition_.notify_all();}
Assets::IndexPtr PlaybackController::imageIndex(){std::lock_guard lock(mutex_);return imageIndex_;}
AudioResources::IndexPtr PlaybackController::audioIndex(){std::lock_guard lock(mutex_);return audioIndex_;}
std::optional<bool> PlaybackController::appliedPatchesEnabled(){std::lock_guard lock(mutex_);if(!imageIndex_)return {};return status_.patchesEnabled;}
void PlaybackController::setHiddenImages(std::unordered_set<std::string> hidden){std::lock_guard lock(mutex_);hidden_=std::move(hidden);status_.hiddenImages=hidden_;filterRequested_=true;queueRefreshLocked();condition_.notify_all();}
void PlaybackController::setImageVisibility(const ImageVisibilityPolicy& policy){std::lock_guard lock(mutex_);imageVisibility_=policy;status_.imageVisibility=policy;filterRequested_=true;queueRefreshLocked();condition_.notify_all();}
void PlaybackController::setCanvasSettings(const CanvasSettings& settings){std::lock_guard lock(mutex_);canvasSettings_=settings;canvasRequested_=true;queueRefreshLocked();condition_.notify_all();}
void PlaybackController::setNegativeImgOffsetsEnabled(bool enabled){std::lock_guard lock(mutex_);if(negativeImgOffsetsEnabled_==enabled)return;negativeImgOffsetsEnabled_=enabled;status_.negativeImgOffsetsEnabled=enabled;negativeImgOffsetsRequested_=true;queueRefreshLocked();condition_.notify_all();}
void PlaybackController::setAudioVolume(float volume){std::lock_guard lock(mutex_);audioVolume_=std::clamp(volume,0.f,1.f);status_.audioVolume=audioVolume_;audioSettingsRequested_=true;condition_.notify_all();}
void PlaybackController::setAudioMuted(bool muted){std::lock_guard lock(mutex_);audioMuted_=muted;status_.audioMuted=muted;audioSettingsRequested_=true;condition_.notify_all();}
void PlaybackController::setHiddenOggs(std::unordered_set<std::string> paths){
    std::lock_guard lock(mutex_);
    if(status_.path.empty()||status_.phase==Phase::Loading||status_.phase==Phase::Error)return;
    std::unordered_set<std::string> normalized;
    for(auto& path:paths){auto key=canonical(path);if(std::find(status_.allOggs.begin(),status_.allOggs.end(),key)!=status_.allOggs.end())normalized.insert(std::move(key));}
    if(hiddenOggs_==normalized)return;
    hiddenOggs_=std::move(normalized);status_.hiddenOggs=hiddenOggs_;
    NavigationRequest request;
    if(navigationRequested_)request=*navigationRequested_;
    else if(activeNavigation_)request=*activeNavigation_;
    else {request.byOrdinal=true;request.ordinal=status_.ordinal;request.time=status_.elapsed;request.preserveState=true;request.resumeOnly=true;request.originalPhase=status_.phase;request.resume=status_.phase==Phase::Playing;}
    request.audioOnly=true;queueNavigationLocked(request,false);
}
void PlaybackController::auditionOgg(std::string path){
    std::lock_guard lock(mutex_);if(status_.path.empty()||status_.phase==Phase::Loading||status_.phase==Phase::Error)return;
    if(status_.phase==Phase::Seeking)return;
    path=canonical(std::move(path));
    if(status_.auditionPath==path||(auditionRequested_&&*auditionRequested_==path))path.clear();
    auditionSerial_++;auditionRequested_=std::move(path);status_.auditionMessage.clear();condition_.notify_all();
}
void PlaybackController::setPlaybackRate(double rate){
    if(!std::isfinite(rate)||rate<.25||rate>4)throw Error("Playback speed must be between 0.25 and 4");
    std::lock_guard lock(mutex_);playbackRate_=status_.playbackRate=rate;playbackRateRequested_=true;condition_.notify_all();
}
void PlaybackController::setHighlightedImage(std::string path){
    {std::lock_guard lock(highlightMutex_);highlightSelection_=path.empty()?std::string{}:normalizeImagePath(std::move(path));}
    presentRequested_=true;condition_.notify_all();
}
void PlaybackController::captureFrame(){std::lock_guard lock(mutex_);if(status_.path.empty())return;captureRequested_=true;condition_.notify_all();}
PlayerStatus PlaybackController::status(){std::lock_guard lock(mutex_);auto result=status_;result.presentationSerial=presentationSerial_.load();{std::lock_guard highlightLock(highlightMutex_);result.highlightedImage=highlightSelection_;result.highlightAvailable=highlightPresentation_.path==result.highlightedImage&&highlightPresentation_.available;}return result;}

void PlaybackController::run(){try{
    Gpu gpu(root_/L"assets"/L"shaders",canvas_);
    auto retained=std::make_shared<RetainedCache>(256*1024*1024);
    AudioResources::IndexPtr audioIndex;
    std::unique_ptr<Assets> assets;std::unique_ptr<Executor> executor;std::unique_ptr<Replay> replay;
    std::unique_ptr<AudioPlayer> audio;std::shared_ptr<std::atomic<uint64_t>> audioEpoch;std::wstring audioMessage;
    std::unique_ptr<AudioPlayer> audition;std::string auditionPath;std::wstring auditionMessage;
    auto stopAudition=[&]{if(audition)audition->stop();audition.reset();auditionPath.clear();};
    Playback playback;std::filesystem::path activeClient,activeReplay;
    ReplayInspector inspection(retained);
    FontWarmup fontWarmup(retained);ImageReadAhead readAhead;
    Clock::time_point endedAt,playingAt;bool frozenChecked=false;uint32_t finalCrc=0;int oldWidth=0,oldHeight=0;
    std::vector<double> frameTimes;
    std::vector<double> presentationGaps,presentationExcess;
    std::optional<Clock::time_point> lastPresentationAt;int32_t lastPresentationTimestamp=0;uint64_t lastPresentationOrdinal=0;
    bool resourceWaiting=false,allowDictionaryPrefetch=false,dictionaryBudgetReached=false;uint64_t dictionarySourceRevision=0;
    int64_t resourceElapsed=0;Clock::time_point resourceWaitAt,lastMaintenance=Clock::now();
    uint64_t bufferingCount=0,longFrames50=0,longFrames100=0;
    double bufferingMilliseconds=0,postStartMax=0;
    std::function<bool()> demandCancelled=[this]{return quitting_||generation_!=consumed_;};
    // One displayed picture survives partial rebuilds. It stores no execution
    // state and is never used to accelerate a seek.
    std::optional<Target> frozenPicture;
    std::shared_ptr<const HighlightFrame> frozenHighlights,frozenOriginalHighlights;
    struct PendingNavigation {NavigationRequest request;uint64_t generation=0,audioSerial=0;bool ended=false;double milliseconds=0;uint32_t crc=0;};
    std::optional<PendingNavigation> pendingNavigation;
    uint64_t audioClockSerial=0;int64_t audioClockTarget=0;
    auto startAudio=[&](int64_t elapsed){audioClockTarget=elapsed;audioClockSerial=audio?audio->seek(elapsed,true):0;};
    auto releaseAudio=[&]{stopAudition();auditionMessage.clear();if(audio)audio->stop();audio.reset();audioEpoch.reset();audioMessage.clear();audioClockSerial=0;};
    auto releaseReplay=[&](bool retainImages=false){
        readAhead.cancel();fontWarmup.cancel();if(assets)assets->cancelPrefetch();resourceWaiting=false;dictionaryBudgetReached=allowDictionaryPrefetch=false;
        pendingNavigation.reset();frozenHighlights.reset();frozenOriginalHighlights.reset();frozenPicture.reset();releaseAudio();gpu.flush();playback=Playback{};replay.reset();executor.reset();
        if(retainImages){gpu.pruneTextures(128*1024*1024);if(assets)assets->pruneDecodedImages(128*1024*1024);}
        else {gpu.resetTextures();if(assets)assets->clearDecodedImages();}activeReplay.clear();retained->trim();
    };
    auto updateAudio=[&]{
        status_.auditionPath=auditionPath;status_.auditionMessage=auditionMessage;
        if(status_.phase==Phase::Empty||status_.phase==Phase::Loading||status_.phase==Phase::Stopped||status_.phase==Phase::Ended||status_.phase==Phase::Error||!audio||!audio->available())status_.currentOggs.clear();
        else if(status_.phase!=Phase::Seeking){
            auto activity=audio->oggActivity();
            if(activity.ready&&activity.seekSerial==audioClockSerial)status_.currentOggs=std::move(activity.paths);
        }
        status_.audioAvailable=audio&&audio->available();status_.audioMessage=audioMessage;
        status_.missingSoundCount=audio?audio->missingResources():0;status_.audioEvents=audio?audio->eventCount():0;
        status_.audioPosition=audio?(status_.phase==Phase::Ended?status_.duration:audio->positionMilliseconds()):0;
        status_.audioRequestedSerial=audio?audioClockSerial:0;
        if(audio){auto failure=audio->error();if(!failure.empty())status_.audioMessage=wide(failure);}
        if(audio){auto stats=audio->stats();status_.audioUnderruns=stats.underruns;status_.audioBufferingEvents=stats.bufferingEvents;status_.audioPcmReadySerial=stats.pcmReadySerial;status_.audioAppliedSerial=stats.appliedSerial;status_.buffering=resourceWaiting||stats.buffering;}
        else {status_.audioUnderruns=status_.audioBufferingEvents=status_.audioPcmReadySerial=status_.audioAppliedSerial=0;status_.buffering=resourceWaiting;}
    };
    struct PreparedAudio {std::unique_ptr<AudioPlayer> player;std::shared_ptr<std::atomic<uint64_t>> epoch;std::wstring message;uint64_t serial=0;};
    auto prepareAudio=[&](const Replay& source,const std::filesystem::path& client,AudioResources::IndexPtr index,uint64_t generation,std::shared_ptr<std::atomic<bool>> aborted=std::make_shared<std::atomic<bool>>(false)){
        PreparedAudio prepared;
        // Visual playback and inspection each own a separate cursor. Scan only
        // recordings with audio instructions, then hand the audio stream to its
        // own sample-clock worker without touching the visual Replay cursor.
        bool recorded=false;for(const auto& [id,command]:source.dictionary)for(const auto& i:command.instructions)
            recorded|=i.opcode==6||i.opcode==7||i.opcode==41;
        if(!recorded)return prepared;
        try{
            prepared.epoch=std::make_shared<std::atomic<uint64_t>>(generation);auto epoch=prepared.epoch;
            auto cancelled=[this,epoch,aborted]{return aborted->load()||quitting_||generation_.load()!=epoch->load();};
            Replay scan(source);auto track=std::make_unique<AudioTrack>(client,root_/L"runtime"/L"cache",scan,cancelled,std::move(index),retained);
            {std::lock_guard lock(mutex_);track->setHiddenOggs(hiddenOggs_);}
            if(!track->hasEvents())return PreparedAudio{};
            if(cancelled())return PreparedAudio{};
            auto notes=track->diagnostics();if(!notes.empty())prepared.message=wide(notes.front());
            prepared.player=std::make_unique<AudioPlayer>(std::move(track));
            prepared.player->setRemainingPrefetchEnabled(false);
            {std::lock_guard lock(mutex_);prepared.player->volume(audioVolume_,audioMuted_);prepared.player->setPlaybackRate(playbackRate_);prepared.player->setHiddenOggs(hiddenOggs_);}
            prepared.serial=prepared.player->seek(0,false,cancelled);
            while(prepared.player->available()&&prepared.player->pcmReadySerial()<prepared.serial){
                if(cancelled())return PreparedAudio{};std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            auto readyNotes=prepared.player->diagnostics();if(!readyNotes.empty())prepared.message=wide(readyNotes.front());
        }catch(const std::exception& error){prepared.player.reset();prepared.epoch.reset();if(!quitting_&&generation_.load()==generation)prepared.message=wide(error.what());}
        return prepared;
    };
    auto applyAudio=[&](PreparedAudio prepared){audio=std::move(prepared.player);audioEpoch=std::move(prepared.epoch);audioMessage=std::move(prepared.message);};
    auto updateImages=[&]{if(!executor)return;status_.currentImages=executor->currentImages();status_.allImages=executor->allImages();status_.hiddenImages=executor->hiddenImages();if(audio)status_.allOggs=audio->oggPaths();};
    auto updateCanvas=[&]{if(!executor)return;const auto& layout=executor->canvasLayout();status_.canvasWidth=layout.width;status_.canvasHeight=layout.height;status_.canvasLeft=layout.left;status_.canvasTop=layout.top;status_.canvasRight=layout.right;status_.canvasBottom=layout.bottom;};
    auto present=[&](bool commit=false){
        presentRequested_=false;if(commit){frozenPicture.reset();frozenHighlights.reset();}
        std::string selection;{std::lock_guard lock(highlightMutex_);selection=highlightSelection_;}auto frame=frozenPicture?frozenHighlights:executor->highlightFrame();
        auto mask=frame?gpu.buildHighlightMask(*frame,selection):nullptr;
        RECT r;GetClientRect(canvas_,&r);gpu.present(frozenPicture?*frozenPicture:executor->output(),std::max(1L,r.right),std::max(1L,r.bottom),mask);
        {std::lock_guard lock(highlightMutex_);highlightPresentation_={selection,mask!=nullptr};}
        oldWidth=r.right;oldHeight=r.bottom;presentationSerial_++;
        auto now=Clock::now();
        if(commit){lastPresentationAt.reset();}
        else if((playback.playing()||playback.ended()||resourceWaiting)&&lastPresentationAt&&playback.ordinal()!=lastPresentationOrdinal){
            auto gap=std::chrono::duration<double,std::milli>(now-*lastPresentationAt).count();
            auto expected=std::max(0,playback.timestamp()-lastPresentationTimestamp)/playback.playbackRate();
            presentationGaps.push_back(gap);presentationExcess.push_back(std::max(0.,gap-expected));
        }
        if(commit||((playback.playing()||playback.ended()||resourceWaiting)&&(!lastPresentationAt||playback.ordinal()!=lastPresentationOrdinal))){lastPresentationAt=now;lastPresentationTimestamp=playback.timestamp();lastPresentationOrdinal=playback.ordinal();}
    };
    auto waitForDemand=[&]{
        if(resourceWaiting||!playback.playing())return;
        auto cancelled=[&]{return demandCancelled&&demandCancelled();};
        resourceWaiting=true;resourceWaitAt=Clock::now();
        if(audio&&audio->available()){
            audio->pauseBuffering(true);
            while(audio->available()&&!audio->stats().buffering){if(cancelled())throw ImagePreparationCancelled{};std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            resourceElapsed=audio->positionMilliseconds();
        }else resourceElapsed=playback.elapsedMilliseconds();
        playback.setElapsedMilliseconds(resourceElapsed);playback.pause();
        {std::lock_guard lock(mutex_);status_.buffering=true;}
        if(cancelled())throw ImagePreparationCancelled{};
    };
    auto finishDemand=[&](bool cancelled=false){
        if(!resourceWaiting)return;
        bufferingMilliseconds+=std::chrono::duration<double,std::milli>(Clock::now()-resourceWaitAt).count();++bufferingCount;
        if(cancelled&&audio)audio->pause();
        if(audio)audio->pauseBuffering(false);
        if(!cancelled){playback.setElapsedMilliseconds(resourceElapsed);playback.resume();}
        resourceWaiting=false;
    };
    while(!quitting_){
        uint64_t generation=generation_;
        if(generation!=consumed_){
            stopAudition();auditionMessage.clear();
            std::filesystem::path path,client;Clock::time_point requestedAt;bool stop,refresh,negativeImgOffsets,patchesEnabled;std::unordered_set<std::string> hidden,hiddenOggs;CanvasSettings settings;ImageVisibilityPolicy visibility;
            {std::lock_guard lock(mutex_);generation=generation_;path=requested_;client=client_;requestedAt=requestedAt_;stop=stopRequested_;hidden=hidden_;hiddenOggs=hiddenOggs_;visibility=imageVisibility_;settings=canvasSettings_;negativeImgOffsets=negativeImgOffsetsEnabled_;patchesEnabled=patchesEnabled_;refresh=std::exchange(refreshImagesRequested_,false);toggleRequested_=false;filterRequested_=false;}
            consumed_=generation;inspection.cancel();fontWarmup.cancel();readAhead.cancel();if(assets)assets->cancelPrefetch();pendingNavigation.reset();playback.stop();finishDemand(true);if(audio){audio->stop();audio->pauseBuffering(false);}
            try{if(refresh||client!=activeClient||!assets){
                releaseReplay();retained->clear();assets.reset();audioIndex.reset();if(!client.empty()){
                    auto nextAssets=std::make_unique<Assets>(client/L"ImagePacks2",patchesEnabled);
                    auto nextAudio=AudioResources::buildIndex(client,patchesEnabled);
                    assets=std::move(nextAssets);audioIndex=std::move(nextAudio);
                }
                activeClient=client;
            }}catch(const std::exception& e){std::lock_guard lock(mutex_);if(generation_==generation){patchesEnabled_=status_.patchesEnabled;status_.phase=Phase::Error;status_.message=wide(e.what());updateAudio();}continue;}
            {std::lock_guard lock(mutex_);if(quitting_||generation_!=generation)continue;imageIndex_=assets?assets->index():Assets::IndexPtr{};audioIndex_=audioIndex;if(assets)status_.patchesEnabled=assets->patchesEnabled();}
            if(stop){releaseAudio();std::lock_guard lock(mutex_);status_.phase=path.empty()?Phase::Empty:Phase::Stopped;status_.client=activeClient;updateAudio();}
            else try{
                if(!assets)throw Error("Select a client before opening a REP");
                if(path!=activeReplay){releaseReplay(true);executor=std::make_unique<Executor>(gpu,*assets,root_/L"runtime"/L"cache",client,retained);}
                else if(!executor)executor=std::make_unique<Executor>(gpu,*assets,root_/L"runtime"/L"cache",client,retained);
                auto options=clientReplayOptions(client,protocol_);
                auto cancelled=[this,generation]{return quitting_||generation_!=generation;};
                ReplayCache replayCache(retained,"interactive/"+utf8(client.wstring())+"/"+std::to_string(reinterpret_cast<uintptr_t>(assets->index().get())));
                auto opened=replayCache.open(path,options,cancelled);executor->enableImageHighlightTracking(true);executor->setCanvasSettings(settings);executor->setNegativeImgOffsetsEnabled(negativeImgOffsets);executor->attach(*opened);executor->setHiddenImages(hidden);executor->setImageVisibility(visibility);executor->setTransparent(true);replay=std::move(opened);
                assets->setDemandWaitCallback(waitForDemand);assets->setDemandCancellation(cancelled);executor->setDemandWaitCallback(waitForDemand);
                activeReplay=path;
                playback.attach(*replay,*executor);
                std::future<PreparedAudio> preparedAudio;
                auto abortAudio=std::make_shared<std::atomic<bool>>(false);
                uint64_t initialAudioSerial=0;
                if(audio){audioEpoch->store(generation);audio->setRemainingPrefetchEnabled(false);audio->pauseBuffering(false);audio->setHiddenOggs(hiddenOggs);initialAudioSerial=audio->seek(0,false,cancelled);}
                else preparedAudio=std::async(std::launch::async,[&,source=std::make_unique<Replay>(*replay),client,index=audioIndex,generation,abortAudio]{return prepareAudio(*source,client,index,generation,abortAudio);});
                struct CancelPendingAudio {std::future<PreparedAudio>& future;std::shared_ptr<std::atomic<bool>> aborted;~CancelPendingAudio(){if(future.valid())aborted->store(true);}} cancelPendingAudio{preparedAudio,abortAudio};
                std::unique_lock<std::mutex> firstPublication(mutex_,std::defer_lock);
                auto committing=[&]{
                    if(preparedAudio.valid()){auto prepared=preparedAudio.get();initialAudioSerial=prepared.serial;if(!cancelled())applyAudio(std::move(prepared));}
                    while(audio&&audio->available()&&audio->pcmReadySerial()<initialAudioSerial){if(cancelled())return false;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
                    if(cancelled())return false;firstPublication.lock();if(cancelled()){firstPublication.unlock();return false;}return true;
                };
                auto before=Clock::now();
                bool first=playback.seekOrdinal(0,cancelled,std::function<void(uint64_t)>{},false,std::function<void(int64_t)>{},committing);
                if(cancelled())continue;
                if(!firstPublication.owns_lock()&&!committing())continue;
                if(audio){audio->volume(audioVolume_,audioMuted_);audio->setPlaybackRate(playbackRate_);}
                playback.setPlaybackRate(playbackRate_);audioClockTarget=0;audioClockSerial=initialAudioSerial;
                present(true);
                if(audio)audio->setRemainingPrefetchEnabled(true);
                playback.finishNavigation(0,first,!first);playingAt=Clock::now();
                if(audio&&audio->available()&&first&&!audio->resumePrepared(initialAudioSerial))throw Error("First audio PCM transaction was superseded");
                auto after=Clock::now();
                frameTimes={std::chrono::duration<double,std::milli>(after-before).count()};
                presentationGaps.clear();presentationExcess.clear();
                bufferingCount=longFrames50=longFrames100=0;bufferingMilliseconds=postStartMax=0;
                status_.p50Frame=status_.p95Frame=status_.p99Frame=0;status_.resourceWaitMilliseconds=0;
                status_.presentationGapP50Milliseconds=status_.presentationGapP95Milliseconds=status_.presentationGapP99Milliseconds=status_.presentationGapMaxMilliseconds=0;
                status_.presentationExcessP95Milliseconds=status_.presentationExcessP99Milliseconds=status_.presentationExcessMaxMilliseconds=0;status_.presentationGap50=status_.presentationGap100=0;
                status_.phase=first?Phase::Playing:Phase::Ended;status_.path=path.wstring();status_.client=activeClient;status_.readySeconds=std::chrono::duration<double>(after-requestedAt).count();status_.processReadySeconds=std::chrono::duration<double>(after-processStart_).count();status_.frames=1;status_.frameCount=0;status_.skipped=0;status_.ordinal=playback.ordinal();status_.timestamp=playback.timestamp();status_.elapsed=0;status_.width=replay->header.width();status_.height=replay->header.height();status_.maxFrameMilliseconds=frameTimes.back();status_.message.clear();status_.frozen=false;status_.compatibilityIgnored=replay->hasCompatibilityIgnored;status_.duration=0;status_.bufferingCount=status_.longFrames50=status_.longFrames100=0;status_.bufferingMilliseconds=status_.postStartMaxFrameMilliseconds=0;updateImages();updateCanvas();updateAudio();frozenChecked=false;
                firstPublication.unlock();
                readAhead.reset(*replay);inspection.submit(*replay,generation,cancelled);
            }catch(const ImageSourcesChanged&){if(!quitting_&&generation_==generation)consumed_=generation-1;}
            catch(const std::exception& e){releaseReplay();std::lock_guard lock(mutex_);if(generation_==generation){status_.phase=Phase::Error;status_.message=wide(e.what());updateAudio();}}
        }
        bool toggle,filter,capture,canvasChanged,audioSettings,muted,negativeImgOffsetsChanged,negativeImgOffsets,scrubChanged,scrubbing,rateChanged;double rate;float volume;std::unordered_set<std::string> hidden,hiddenOggs;CanvasSettings settings;ImageVisibilityPolicy visibility;std::optional<NavigationRequest> navigation;std::optional<std::string> auditionRequest;uint64_t auditionTicket;
        {std::lock_guard lock(mutex_);rateChanged=std::exchange(playbackRateRequested_,false);rate=playbackRate_;toggle=std::exchange(toggleRequested_,false);filter=std::exchange(filterRequested_,false);capture=std::exchange(captureRequested_,false);canvasChanged=std::exchange(canvasRequested_,false);audioSettings=std::exchange(audioSettingsRequested_,false);negativeImgOffsetsChanged=std::exchange(negativeImgOffsetsRequested_,false);negativeImgOffsets=negativeImgOffsetsEnabled_;muted=audioMuted_;volume=audioVolume_;scrubChanged=std::exchange(scrubChanged_,false);scrubbing=scrubbing_;hidden=hidden_;visibility=imageVisibility_;settings=canvasSettings_;
            if((filter||canvasChanged||negativeImgOffsetsChanged)&&!navigationRequested_&&!activeNavigation_)queueRefreshLocked();
            navigation=std::exchange(navigationRequested_,{});if(navigation)activeNavigation_=navigation;
            hiddenOggs=hiddenOggs_;auditionRequest=std::exchange(auditionRequested_,{});auditionTicket=auditionSerial_;
        }
        if(audioSettings){if(audio)audio->volume(volume,muted);if(audition)audition->volume(volume,muted);}
        if(auditionRequest){stopAudition();auditionMessage.clear();}
        if(rateChanged){playback.setPlaybackRate(rate);if(audio)audio->setPlaybackRate(rate);}
        if(!replay||!executor)presentRequested_=false;
        if(replay&&executor)try{
            if(auditionRequest&&!auditionRequest->empty()){
                auto cancelled=[this,generation,auditionTicket]{return quitting_||generation_!=generation||auditionSerial_!=auditionTicket;};
                try{
                    auto elapsed=playback.elapsedMilliseconds();
                    if(audio&&audio->available()){
                        audio->pauseBuffering(true);
                        while(audio->available()&&!audio->stats().buffering&&!cancelled())std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        elapsed=audio->positionMilliseconds();audio->pause();audio->pauseBuffering(false);
                    }
                    if(!cancelled()){
                        playback.finishNavigation(elapsed,false);
                        {std::lock_guard lock(mutex_);if(!cancelled()){
                            // A mute refresh queued before this pause must not
                            // restore its earlier Playing intent over audition.
                            auto pausedRefresh=[&](auto& request){if(request&&request->audioOnly){request->resume=false;request->originalPhase=Phase::Paused;if(request->preserveState)request->time=elapsed;}};
                            pausedRefresh(navigation);pausedRefresh(navigationRequested_);pausedRefresh(activeNavigation_);
                            status_.phase=navigation||navigationRequested_?Phase::Seeking:Phase::Paused;status_.elapsed=elapsed;updateAudio();
                        }}
                        std::shared_ptr<const AudioClip> clip;
                        if(audio)clip=audio->oggClip(*auditionRequest,cancelled);
                        else {Replay source(*replay);AudioTrack catalog(activeClient,root_/L"runtime"/L"cache",source,cancelled,audioIndex,retained);clip=catalog.oggClip(*auditionRequest,cancelled);}
                        if(!cancelled()){
                            if(!clip||!clip->frames())throw Error("OGG 文件缺失或无法读取 / OGG missing or unreadable");
                            auto candidate=std::make_unique<AudioPlayer>(std::make_unique<AudioTrack>(std::move(clip)));
                            if(!candidate->available())throw Error(candidate->error());
                            candidate->setRemainingPrefetchEnabled(false);
                            {std::lock_guard lock(mutex_);candidate->volume(audioVolume_,audioMuted_);}
                            auto serial=candidate->seek(0,false,cancelled);
                            while(candidate->available()&&candidate->pcmReadySerial()<serial&&!cancelled())std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            std::lock_guard lock(mutex_);
                            if(!cancelled()){
                                if(!candidate->resumePrepared(serial))throw Error("OGG 试听无法启动 / Cannot start OGG audition");
                                audition=std::move(candidate);auditionPath=*auditionRequest;updateAudio();
                            }
                        }
                    }
                }catch(const std::exception& error){if(!cancelled()){auditionMessage=wide(error.what());std::lock_guard lock(mutex_);updateAudio();}}
            }
            if(audition&&(audition->positionMilliseconds()>=audition->durationMilliseconds()||!audition->available())){
                auditionMessage=wide(audition->error());stopAudition();std::lock_guard lock(mutex_);updateAudio();
            }
            if(toggle){lastPresentationAt.reset();
                if(playback.playing()){playback.pause();if(audio)audio->pause();}
                else if(playback.paused()){playback.resume();startAudio(playback.elapsedMilliseconds());}
                else {this->replay();continue;}
                std::lock_guard lock(mutex_);status_.phase=playback.paused()?Phase::Paused:Phase::Playing;updateAudio();
            }
            if(auto info=inspection.take(generation)){executor->setInspection(*info);std::lock_guard lock(mutex_);status_.duration=info->durationMilliseconds;status_.frameCount=info->scenes;updateImages();}
            if(scrubChanged&&scrubbing){auto elapsed=playback.elapsedMilliseconds();playback.finishNavigation(elapsed,false);if(audio)audio->pause();std::lock_guard lock(mutex_);if(scrubbing_){status_.elapsed=elapsed;status_.scrubbing=true;}}
            // A successful gate keeps the request lock until executor state,
            // displayed pixels and the final public playback status agree.
            std::unique_lock<std::mutex> publicationLock(mutex_,std::defer_lock);
            if(navigation){
                readAhead.cancel();fontWarmup.cancel();assets->cancelPrefetch();
                pendingNavigation.reset();auto request=*navigation;auto before=Clock::now();
                playback.finishNavigation(playback.elapsedMilliseconds(),false);if(audio)audio->pause();
                auto cancelled=[this,generation,serial=request.serial]{return quitting_||generation_!=generation||navigationSerial_!=serial;};
                DemandCancellationScope demandScope(*assets,demandCancelled,cancelled);
                bool restoredAudio=false;
                // Stop releases the device player but retains the replay. A
                // subsequent seek/step must rebuild its target sound state.
                if(!audio&&!(request.preserveState&&request.originalPhase==Phase::Stopped)){
                    auto aborted=std::make_shared<std::atomic<bool>>(false);
                    auto future=std::async(std::launch::async,[&,source=std::make_unique<Replay>(*replay),index=audioIndex,generation,aborted]{return prepareAudio(*source,activeClient,index,generation,aborted);});
                    while(future.wait_for(std::chrono::milliseconds(1))!=std::future_status::ready)if(cancelled())aborted->store(true);
                    auto prepared=future.get();if(cancelled())continue;applyAudio(std::move(prepared));restoredAudio=bool(audio);
                }
                uint64_t navigationAudioSerial=0;int64_t navigationAudioTarget=request.time;
                auto preparing=[&](int64_t elapsed,bool force=false){
                    bool changed=navigationAudioTarget!=elapsed;navigationAudioTarget=elapsed;
                    if(audio&&(force||restoredAudio||request.audioOnly||!request.preserveState||request.resume)&&(!navigationAudioSerial||changed)){
                        audio->setHiddenOggs(hiddenOggs);
                        navigationAudioSerial=audio->seek(elapsed,false,cancelled);audioClockSerial=navigationAudioSerial;audioClockTarget=elapsed;
                    }
                };
                // Time seeks can prepare audio while the independent visual
                // cursor reads to its target. Ordinal seeks prepare as soon as
                // their selected timestamp is known, before its GPU execution.
                if(!request.byOrdinal||request.preserveState)preparing(request.time);
                if(!frozenPicture){auto& output=executor->output();gpu.flush();frozenPicture=gpu.target(output.image.width,output.image.height,false);gpu.context()->CopyResource(frozenPicture->image.texture.get(),output.image.texture.get());frozenHighlights=executor->highlightFrame();}
                if(canvasChanged)executor->setCanvasSettings(settings);
                if(negativeImgOffsetsChanged)executor->setNegativeImgOffsetsEnabled(negativeImgOffsets);
                if(filter){executor->setHiddenImages(hidden);executor->setImageVisibility(visibility);}
                auto progress=[&](uint64_t ordinal){std::lock_guard lock(mutex_);if(navigationSerial_==request.serial&&generation_==generation)status_.navigationProgressOrdinal=ordinal;};
                auto committing=[&]{
                    while(true){
                        bool resume=request.resume;{std::lock_guard lock(mutex_);if(activeNavigation_&&activeNavigation_->serial==request.serial)resume=activeNavigation_->resume;}
                        if(resume&&!navigationAudioSerial)preparing(navigationAudioTarget,true);
                        while(audio&&audio->available()&&navigationAudioSerial&&audio->pcmReadySerial()<navigationAudioSerial){
                            if(cancelled())return false;std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        }
                        publicationLock.lock();
                        if(cancelled()){publicationLock.unlock();return false;}
                        // Space can request resume while audio was preparing.
                        // Never retain the publication lock across that wait.
                        resume=activeNavigation_.value_or(request).resume;
                        if(audio&&audio->available()&&resume&&(!navigationAudioSerial||audio->pcmReadySerial()<navigationAudioSerial)){
                            publicationLock.unlock();preparing(navigationAudioTarget,true);continue;
                        }
                        return true;
                    }
                };
                if(request.resumeOnly&&!request.redraw&&!filter&&!canvasChanged&&!negativeImgOffsetsChanged){
                    // Resume the frozen picture after the device has completed
                    // its seek; no scene-state rebuild is needed for playback.
                }else if(request.redraw||request.resumeOnly)playback.refresh(cancelled,committing);
                else if(request.byOrdinal)playback.seekOrdinal(request.ordinal,cancelled,progress,false,[&](int64_t elapsed){preparing(elapsed);},committing);
                else playback.seek(request.time,request.last,cancelled,progress,[&](int64_t elapsed){preparing(elapsed);},committing);
                if(!cancelled()){
                    if(request.preserveState)playback.setElapsedMilliseconds(request.time);
                    uint32_t pixelsCrc=test_?crc(gpu.readback(executor->output())):0;
                    // Validate and publish together, so a newer UI request cannot
                    // interleave an obsolete picture or audio seek submission.
                    if(!publicationLock.owns_lock())publicationLock.lock();
                    if(generation_==generation&&navigationSerial_==request.serial){
                        present(true);
                        // A paused visual refresh leaves the frozen device clock.
                        pendingNavigation=PendingNavigation{request,generation,navigationAudioSerial,playback.ended(),std::chrono::duration<double,std::milli>(Clock::now()-before).count(),pixelsCrc};
                    }
                }else {if(!publicationLock.owns_lock())publicationLock.lock();if(activeNavigation_&&activeNavigation_->serial==request.serial)activeNavigation_.reset();}
            }
            if(pendingNavigation){
                auto& pending=*pendingNavigation;
                if(generation_!=pending.generation||navigationSerial_!=pending.request.serial)pendingNavigation.reset();
                else if(!audio||!audio->available()||audio->pcmReadySerial()>=pending.audioSerial){
                    if(!publicationLock.owns_lock())publicationLock.lock();
                    if(generation_==pending.generation&&navigationSerial_==pending.request.serial){
                        auto request=activeNavigation_.value_or(pending.request);
                        bool ended=request.last||pending.ended||(request.preserveState&&request.originalPhase==Phase::Ended);
                        bool stopped=request.preserveState&&request.originalPhase==Phase::Stopped;
                        bool resume=request.resume&&!ended&&!stopped;
                        if(audio&&audio->available()&&resume&&(pending.audioSerial==0||!audio->resumePrepared(pending.audioSerial))){
                            // Space may request resume after a paused visual
                            // rebuild began without repositioning audio.
                            pending.audioSerial=audio->seek(playback.elapsedMilliseconds(),false,[this,generation,serial=request.serial]{return quitting_||generation_!=generation||navigationSerial_!=serial;});
                            audioClockSerial=pending.audioSerial;audioClockTarget=playback.elapsedMilliseconds();
                            continue;
                        }
                        playback.finishNavigation(playback.elapsedMilliseconds(),resume,ended,stopped);
                        status_.phase=ended?Phase::Ended:stopped?Phase::Stopped:resume?Phase::Playing:Phase::Paused;
                        status_.scrubbing=false;status_.timestamp=playback.timestamp();status_.ordinal=playback.ordinal();status_.elapsed=playback.elapsedMilliseconds();status_.skipped=playback.skipped;
                        status_.frames++;if(test_){status_.frameCrc=pending.crc;status_.captureSerial++;}
                        status_.completedNavigationSerial=request.serial;status_.navigationProgressOrdinal=playback.ordinal();status_.maxFrameMilliseconds=std::max(status_.maxFrameMilliseconds,pending.milliseconds);status_.frozen=false;
                        updateCanvas();updateImages();updateAudio();activeNavigation_.reset();frozenChecked=false;
                        readAhead.reset(*replay);
                    }
                    pendingNavigation.reset();
                }
            }
            if(publicationLock.owns_lock())publicationLock.unlock();
            bool seeking;{std::lock_guard lock(mutex_);seeking=status_.phase==Phase::Seeking;}
            if(seeking&&presentRequested_.load())present();
            if(seeking){std::unique_lock lock(mutex_);condition_.wait_for(lock,std::chrono::milliseconds(2),[&]{return quitting_||generation_!=consumed_||navigationRequested_||presentRequested_.load()||scrubChanged_||filterRequested_||canvasRequested_||negativeImgOffsetsRequested_;});continue;}
            // The device sample clock includes seek/start buffering and the
            // selected frequency ratio, so pictures follow the actual sound.
            // A reused device still holds the previous replay's clock until
            // the new start seek is applied. Keep its requested start meanwhile.
            std::optional<int64_t> deviceTarget;
            if(playback.playing()&&audio&&audio->available()){
                deviceTarget=audio->appliedSeekSerial()>=audioClockSerial?audio->positionMilliseconds():audioClockTarget;
                playback.setElapsedMilliseconds(*deviceTarget);
            }
            auto serial=navigationSerial_.load();auto normalCancelled=[this,generation,serial]{return quitting_||generation_!=generation||navigationSerial_!=serial;};
            DemandCancellationScope demandScope(*assets,demandCancelled,normalCancelled);
            std::unique_lock<std::mutex> normalPublication(mutex_,std::defer_lock);
            auto committing=[&]{normalPublication.lock();if(normalCancelled()){normalPublication.unlock();return false;}return true;};
            auto before=Clock::now();bool changed;
            if(playback.hasScene()&&!playback.hasPendingSelection()&&executor->sourceRevision()!=assets->sourceRevision()){
                waitForDemand();changed=playback.refresh(normalCancelled,committing);
            }else changed=resourceWaiting?playback.select(resourceElapsed,normalCancelled,committing):deviceTarget?playback.select(*deviceTarget,normalCancelled,committing):playback.tick(normalCancelled,committing);
            RECT r;GetClientRect(canvas_,&r);bool resized=r.right!=oldWidth||r.bottom!=oldHeight;
            if(!normalPublication.owns_lock())normalPublication.lock();
            if(normalCancelled()){normalPublication.unlock();finishDemand(true);continue;}
            if((changed||resized||presentRequested_.load())&&playback.hasScene())present();
            if(!playback.hasScene())presentRequested_=false;
            finishDemand();
            if(changed){double ms=std::chrono::duration<double,std::milli>(Clock::now()-before).count();frameTimes.push_back(ms);postStartMax=std::max(postStartMax,ms);longFrames50+=ms>50;longFrames100+=ms>100;status_.frames++;status_.skipped=playback.skipped;status_.timestamp=playback.timestamp();status_.ordinal=playback.ordinal();status_.maxFrameMilliseconds=std::max(status_.maxFrameMilliseconds,ms);updateImages();}
            status_.elapsed=deviceTarget&&!resourceWaiting?*deviceTarget:playback.elapsedMilliseconds();if(status_.duration)status_.elapsed=std::min<int64_t>(status_.elapsed,status_.duration);status_.bufferingCount=bufferingCount;status_.bufferingMilliseconds=bufferingMilliseconds;status_.resourceWaitMilliseconds=bufferingMilliseconds;status_.postStartMaxFrameMilliseconds=postStartMax;status_.longFrames50=longFrames50;status_.longFrames100=longFrames100;updateAudio();
            normalPublication.unlock();
            if(Clock::now()-lastMaintenance>=std::chrono::milliseconds(100)){
                retained->trim();auto stats=retained->stats();
                auto revision=assets->sourceRevision();if(revision!=dictionarySourceRevision){dictionaryBudgetReached=false;dictionarySourceRevision=revision;}
                // The active image working set grows until a source revision
                // or REP boundary. Stop recounting it once headroom is gone.
                if(!dictionaryBudgetReached){allowDictionaryPrefetch=assets->decodedImageBytes()<64*1024*1024;dictionaryBudgetReached=!allowDictionaryPrefetch;}
                lastMaintenance=Clock::now();
                std::lock_guard lock(mutex_);status_.retainedCacheBytes=stats.retainedBytes;status_.inactiveCacheBytes=stats.inactiveBytes;status_.activeCacheBytes=stats.activeBytes;status_.cacheEvictions=stats.evictions;
            }
            if(!playback.ended()){
                auto cancelled=[this,generation]{return quitting_||generation_!=generation;};
                readAhead.advance(*assets,fontWarmup,activeClient,playback.elapsedMilliseconds(),generation,allowDictionaryPrefetch,cancelled);
                auto budget=Clock::now()+std::chrono::milliseconds(1);
                while(!cancelled()&&Clock::now()<budget){auto ready=assets->drainReady(generation,1);if(ready.empty())break;auto revision=assets->sourceRevision();for(auto& frame:ready)if(frame&&!frame->empty&&!cancelled()){gpu.texture(frame->texture);if(assets->sourceRevision()!=revision)gpu.forgetTexture(frame->texture.get());}}
            }
            if(capture){auto pixels=gpu.readback(frozenPicture?*frozenPicture:executor->output());std::lock_guard lock(mutex_);status_.frameCrc=crc(pixels);status_.captureSerial++;}
            if(playback.ended()){
                bool newEnd=false;{std::lock_guard lock(mutex_);if(status_.phase==Phase::Playing){status_.phase=Phase::Ended;status_.playbackSeconds=std::chrono::duration<double>(Clock::now()-playingAt).count();status_.duration=std::max(status_.duration,status_.timestamp);status_.elapsed=status_.duration;newEnd=true;}}
                if(newEnd){readAhead.cancel();fontWarmup.cancel();assets->cancelPrefetch();if(audio)audio->seek(playback.timestamp(),false);{std::lock_guard lock(mutex_);updateAudio();}endedAt=Clock::now();if(test_){auto pixels=gpu.readback(executor->output());finalCrc=crc(pixels);std::ofstream out(root_/L"validation"/L"ui_last_frame.rgba",std::ios::binary);out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());}
                    auto samples=frameTimes,gaps=presentationGaps,excess=presentationExcess;
                    std::sort(samples.begin(),samples.end());std::sort(gaps.begin(),gaps.end());std::sort(excess.begin(),excess.end());
                    auto percentile=[](const auto& values,double p){return values.empty()?0.:values[std::min(values.size()-1,size_t(p*(values.size()-1)))];};
                    std::lock_guard lock(mutex_);status_.p50Frame=percentile(samples,.50);status_.p95Frame=percentile(samples,.95);status_.p99Frame=percentile(samples,.99);
                    status_.presentationGapP50Milliseconds=percentile(gaps,.50);status_.presentationGapP95Milliseconds=percentile(gaps,.95);status_.presentationGapP99Milliseconds=percentile(gaps,.99);status_.presentationGapMaxMilliseconds=percentile(gaps,1);
                    status_.presentationExcessP95Milliseconds=percentile(excess,.95);status_.presentationExcessP99Milliseconds=percentile(excess,.99);status_.presentationExcessMaxMilliseconds=percentile(excess,1);
                    status_.presentationGap50=std::count_if(gaps.begin(),gaps.end(),[](double value){return value>50;});status_.presentationGap100=std::count_if(gaps.begin(),gaps.end(),[](double value){return value>100;});
                }
                if(test_&&!frozenChecked&&Clock::now()-endedAt>std::chrono::milliseconds(800)){bool same=crc(gpu.readback(executor->output()))==finalCrc;std::lock_guard lock(mutex_);status_.frozen=same;status_.finalCrc=finalCrc;frozenChecked=true;}
            }
        }catch(const ImageSourcesChanged&){
            if(quitting_||generation_!=generation){finishDemand(true);continue;}
            if(navigation){std::lock_guard lock(mutex_);if(navigationSerial_==navigation->serial){navigationRequested_=activeNavigation_.value_or(*navigation);status_.phase=Phase::Seeking;}}
            else {try{waitForDemand();}catch(...){finishDemand(true);}}
            continue;
        }catch(const std::exception& e){finishDemand(true);if(quitting_||generation_!=generation)continue;releaseReplay();std::lock_guard lock(mutex_);status_.phase=Phase::Error;status_.message=wide(e.what());updateAudio();}
        std::unique_lock lock(mutex_);condition_.wait_for(lock,std::chrono::milliseconds(playback.playing()||pendingNavigation||audition?1:20),[&]{return quitting_||generation_!=consumed_||presentRequested_.load()||toggleRequested_||filterRequested_||captureRequested_||canvasRequested_||audioSettingsRequested_||playbackRateRequested_||negativeImgOffsetsRequested_||scrubChanged_||navigationRequested_||auditionRequested_.has_value();});
    }
    inspection.cancel();gpu.flush();
}catch(const std::exception& e){std::lock_guard lock(mutex_);status_.phase=Phase::Error;status_.message=wide(e.what());status_.audioAvailable=false;status_.audioEvents=status_.missingSoundCount=0;status_.audioPosition=0;status_.audioMessage.clear();}}
}
