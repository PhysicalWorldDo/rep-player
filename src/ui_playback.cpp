#include "ui_playback.hpp"
#include "audio.hpp"
#include <algorithm>
#include <fstream>
#include <future>
#include <utility>
#include <cmath>

namespace rep::ui {
PlaybackController::PlaybackController(HWND canvas,std::filesystem::path root,std::filesystem::path client,bool test,Clock::time_point processStart,ClientProtocolSelection protocol,CanvasSettings settings,bool patchesEnabled)
    :canvas_(canvas),root_(std::move(root)),client_(std::move(client)),protocol_(std::move(protocol)),canvasSettings_(std::move(settings)),test_(test),processStart_(processStart),patchesEnabled_(patchesEnabled) {
    status_.patchesEnabled=patchesEnabled;
    thread_=std::thread([this]{run();});
}
PlaybackController::~PlaybackController(){quitting_=true;generation_++;condition_.notify_all();if(thread_.joinable())thread_.join();}
void PlaybackController::open(const std::filesystem::path& path){std::lock_guard lock(mutex_);requested_=path;requestedAt_=Clock::now();stopRequested_=false;navigationRequested_.reset();activeNavigation_.reset();navigationSerial_++;scrubbing_=status_.scrubbing=false;hidden_.clear();imageVisibility_.clearManual();status_.imageVisibility=imageVisibility_;status_.frameCount=0;status_.hiddenImages.clear();status_.currentImages.clear();status_.allImages.clear();status_.phase=Phase::Loading;status_.compatibilityIgnored=false;status_.message.clear();status_.path=path.wstring();generation_++;condition_.notify_all();}
void PlaybackController::replay(){auto s=status();if(s.path.empty())return;std::lock_guard lock(mutex_);requested_=s.path;requestedAt_=Clock::now();stopRequested_=false;navigationRequested_.reset();activeNavigation_.reset();navigationSerial_++;scrubbing_=status_.scrubbing=false;status_.phase=Phase::Loading;generation_++;condition_.notify_all();}
void PlaybackController::stop(){std::lock_guard lock(mutex_);stopRequested_=true;navigationRequested_.reset();activeNavigation_.reset();navigationSerial_++;scrubbing_=status_.scrubbing=false;generation_++;condition_.notify_all();}
void PlaybackController::togglePause(){
    std::lock_guard lock(mutex_);if(status_.path.empty())return;
    if(status_.phase==Phase::Seeking){
        if(scrubbing_)scrubResume_=!scrubResume_;
        if(navigationRequested_)navigationRequested_->resume=!navigationRequested_->resume;
        if(activeNavigation_)activeNavigation_->resume=!activeNavigation_->resume;
    }else if(status_.phase==Phase::Paused){
        NavigationRequest request;request.byOrdinal=true;request.ordinal=status_.ordinal;request.time=status_.elapsed;
        request.preserveState=true;request.resumeOnly=true;request.resume=true;request.originalPhase=Phase::Paused;
        queueNavigationLocked(request);
    }else toggleRequested_=true;
    condition_.notify_all();
}
void PlaybackController::queueNavigationLocked(NavigationRequest request){
    request.serial=++navigationSerial_;navigationRequested_=request;status_.phase=Phase::Seeking;
    status_.navigationSerial=request.serial;status_.navigationTarget=request.time;status_.navigationProgressOrdinal=0;
    condition_.notify_all();
}
void PlaybackController::queueRefreshLocked(){
    if(status_.path.empty()||status_.phase==Phase::Loading||status_.phase==Phase::Error)return;
    NavigationRequest request;
    if(navigationRequested_)request=*navigationRequested_;
    else if(activeNavigation_)request=*activeNavigation_;
    else {request.byOrdinal=true;request.ordinal=status_.ordinal;request.time=status_.elapsed;request.preserveState=true;request.originalPhase=status_.phase;request.resume=status_.phase==Phase::Playing;}
    request.forceRebuild=true;queueNavigationLocked(request);
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
    scrubOriginalPhase_=status_.phase==Phase::Seeking?Phase::Paused:status_.phase;
    scrubResume_=status_.phase==Phase::Playing||(navigationRequested_&&navigationRequested_->resume)||(activeNavigation_&&activeNavigation_->resume);
    navigationRequested_.reset();activeNavigation_.reset();status_.navigationSerial=++navigationSerial_;status_.navigationTarget=status_.elapsed;scrubbing_=true;scrubChanged_=true;status_.phase=Phase::Seeking;condition_.notify_all();
}
void PlaybackController::endScrub(std::optional<int64_t> target,bool endpointLast){
    std::lock_guard lock(mutex_);if(!scrubbing_)return;scrubbing_=false;scrubChanged_=true;
    NavigationRequest request;request.resume=scrubResume_;
    if(target){request.time=std::max<int64_t>(0,*target);request.last=endpointLast;}
    else {request.byOrdinal=true;request.ordinal=status_.ordinal;request.time=status_.elapsed;request.preserveState=true;request.originalPhase=scrubOriginalPhase_;}
    queueNavigationLocked(request);
}
void PlaybackController::seekTime(int64_t target,bool endpointLast){
    std::lock_guard lock(mutex_);if(status_.path.empty()||status_.phase==Phase::Loading||status_.phase==Phase::Error)return;
    NavigationRequest request;request.time=std::max<int64_t>(0,target);request.last=endpointLast;
    request.resume=status_.phase==Phase::Playing||(navigationRequested_&&navigationRequested_->resume)||(activeNavigation_&&activeNavigation_->resume);
    queueNavigationLocked(request);
}
void PlaybackController::configureClient(const std::filesystem::path& client){std::lock_guard lock(mutex_);client_=client;requested_.clear();hidden_.clear();imageVisibility_.clearManual();navigationRequested_.reset();activeNavigation_.reset();navigationSerial_++;scrubbing_=false;stopRequested_=true;toggleRequested_=filterRequested_=captureRequested_=false;refreshImagesRequested_=true;imageIndex_.reset();status_=PlayerStatus{};status_.playbackRate=playbackRate_;status_.client=client_;status_.audioVolume=audioVolume_;status_.audioMuted=audioMuted_;status_.negativeImgOffsetsEnabled=negativeImgOffsetsEnabled_;status_.patchesEnabled=patchesEnabled_;status_.imageVisibility=imageVisibility_;generation_++;condition_.notify_all();}
void PlaybackController::refreshImages(std::optional<bool> patchesEnabled){std::lock_guard lock(mutex_);if(client_.empty())return;if(patchesEnabled)patchesEnabled_=*patchesEnabled;requested_=status_.path;requestedAt_=Clock::now();stopRequested_=requested_.empty();navigationRequested_.reset();activeNavigation_.reset();navigationSerial_++;scrubbing_=status_.scrubbing=false;refreshImagesRequested_=true;imageIndex_.reset();status_.phase=Phase::Loading;status_.frameCount=0;status_.currentImages.clear();status_.allImages.clear();status_.message.clear();generation_++;condition_.notify_all();}
Assets::IndexPtr PlaybackController::imageIndex(){std::lock_guard lock(mutex_);return imageIndex_;}
std::optional<bool> PlaybackController::appliedPatchesEnabled(){std::lock_guard lock(mutex_);if(!imageIndex_)return {};return status_.patchesEnabled;}
void PlaybackController::setHiddenImages(std::unordered_set<std::string> hidden){std::lock_guard lock(mutex_);hidden_=std::move(hidden);status_.hiddenImages=hidden_;filterRequested_=true;queueRefreshLocked();condition_.notify_all();}
void PlaybackController::setImageVisibility(const ImageVisibilityPolicy& policy){std::lock_guard lock(mutex_);imageVisibility_=policy;status_.imageVisibility=policy;filterRequested_=true;queueRefreshLocked();condition_.notify_all();}
void PlaybackController::setCanvasSettings(const CanvasSettings& settings){std::lock_guard lock(mutex_);canvasSettings_=settings;canvasRequested_=true;queueRefreshLocked();condition_.notify_all();}
void PlaybackController::setNegativeImgOffsetsEnabled(bool enabled){std::lock_guard lock(mutex_);if(negativeImgOffsetsEnabled_==enabled)return;negativeImgOffsetsEnabled_=enabled;status_.negativeImgOffsetsEnabled=enabled;negativeImgOffsetsRequested_=true;queueRefreshLocked();condition_.notify_all();}
void PlaybackController::setAudioVolume(float volume){std::lock_guard lock(mutex_);audioVolume_=std::clamp(volume,0.f,1.f);status_.audioVolume=audioVolume_;audioSettingsRequested_=true;condition_.notify_all();}
void PlaybackController::setAudioMuted(bool muted){std::lock_guard lock(mutex_);audioMuted_=muted;status_.audioMuted=muted;audioSettingsRequested_=true;condition_.notify_all();}
void PlaybackController::setPlaybackRate(double rate){
    if(!std::isfinite(rate)||rate<.25||rate>4)throw Error("Playback speed must be between 0.25 and 4");
    std::lock_guard lock(mutex_);playbackRate_=status_.playbackRate=rate;playbackRateRequested_=true;condition_.notify_all();
}
void PlaybackController::captureFrame(){std::lock_guard lock(mutex_);if(status_.path.empty())return;captureRequested_=true;condition_.notify_all();}
PlayerStatus PlaybackController::status(){std::lock_guard lock(mutex_);return status_;}

void PlaybackController::run(){try{
    Gpu gpu(root_/L"assets"/L"shaders",canvas_);gpu.createAllPrograms();
    std::unique_ptr<Assets> assets;std::unique_ptr<Executor> executor;std::unique_ptr<Replay> replay;
    std::unique_ptr<AudioPlayer> audio;std::shared_ptr<std::atomic<uint64_t>> audioEpoch;std::wstring audioMessage;
    Playback playback;std::filesystem::path activeClient,activeReplay;
    std::future<ReplayInspection> inspection;uint64_t inspectedGeneration=0;
    Clock::time_point endedAt,playingAt;bool frozenChecked=false;uint32_t finalCrc=0;int oldWidth=0,oldHeight=0;
    std::vector<double> frameTimes;
    // One displayed picture survives partial rebuilds. It stores no execution
    // state and is never used to accelerate a seek.
    std::optional<Target> frozenPicture;
    struct PendingNavigation {NavigationRequest request;uint64_t generation=0,audioSerial=0;bool ended=false;double milliseconds=0;uint32_t crc=0;};
    std::optional<PendingNavigation> pendingNavigation;
    uint64_t audioClockSerial=0;int64_t audioClockTarget=0;
    auto startAudio=[&](int64_t elapsed){audioClockTarget=elapsed;audioClockSerial=audio?audio->seek(elapsed,true):0;};
    auto releaseAudio=[&]{if(audio)audio->stop();audio.reset();audioEpoch.reset();audioMessage.clear();audioClockSerial=0;};
    auto releaseReplay=[&]{pendingNavigation.reset();frozenPicture.reset();releaseAudio();gpu.flush();playback=Playback{};replay.reset();executor.reset();gpu.resetTextures();if(assets)assets->clearDecodedImages();activeReplay.clear();};
    auto updateAudio=[&]{
        status_.audioAvailable=audio&&audio->available();status_.audioMessage=audioMessage;
        status_.missingSoundCount=audio?audio->missingResources():0;status_.audioEvents=audio?audio->eventCount():0;
        status_.audioPosition=audio?(status_.phase==Phase::Ended?status_.duration:audio->positionMilliseconds()):0;
        if(audio){auto failure=audio->error();if(!failure.empty())status_.audioMessage=wide(failure);}
    };
    auto initializeAudio=[&](const std::filesystem::path& path,Replay& source,uint64_t generation){
        if(audio){audioEpoch->store(generation);audio->stop();return;}
        // Visual playback and inspection each own a separate cursor. Scan only
        // recordings with audio instructions, then hand the audio stream to its
        // own sample-clock worker without touching the visual Replay cursor.
        bool recorded=false;for(const auto& [id,command]:source.dictionary)for(const auto& i:command.instructions)
            recorded|=i.opcode==6||i.opcode==7||i.opcode==41;
        if(!recorded)return;
        try{
            audioEpoch=std::make_shared<std::atomic<uint64_t>>(generation);auto epoch=audioEpoch;
            auto cancelled=[this,epoch]{return quitting_||generation_.load()!=epoch->load();};
            Replay scan(path,source.options);auto track=std::make_unique<AudioTrack>(activeClient,root_/L"runtime"/L"cache",scan,cancelled);
            if(!track->hasEvents()){audioEpoch.reset();return;}
            track->prepare(0,2000,cancelled);if(cancelled())return;
            auto notes=track->diagnostics();if(!notes.empty())audioMessage=wide(notes.front());
            audio=std::make_unique<AudioPlayer>(std::move(track));
        }catch(const std::exception& error){releaseAudio();if(!quitting_&&generation_.load()==generation)audioMessage=wide(error.what());}
    };
    auto updateImages=[&]{if(!executor)return;status_.currentImages=executor->currentImages();status_.allImages=executor->allImages();status_.hiddenImages=executor->hiddenImages();};
    auto updateCanvas=[&]{if(!executor)return;const auto& layout=executor->canvasLayout();status_.canvasWidth=layout.width;status_.canvasHeight=layout.height;status_.canvasLeft=layout.left;status_.canvasTop=layout.top;status_.canvasRight=layout.right;status_.canvasBottom=layout.bottom;};
    auto present=[&](bool commit=false){if(commit)frozenPicture.reset();RECT r;GetClientRect(canvas_,&r);gpu.present(frozenPicture?*frozenPicture:executor->output(),std::max(1L,r.right),std::max(1L,r.bottom));oldWidth=r.right;oldHeight=r.bottom;};
    while(!quitting_){
        uint64_t generation=generation_;
        if(generation!=consumed_){
            std::filesystem::path path,client;Clock::time_point requestedAt;bool stop,refresh,negativeImgOffsets,patchesEnabled;std::unordered_set<std::string> hidden;CanvasSettings settings;ImageVisibilityPolicy visibility;
            {std::lock_guard lock(mutex_);generation=generation_;path=requested_;client=client_;requestedAt=requestedAt_;stop=stopRequested_;hidden=hidden_;visibility=imageVisibility_;settings=canvasSettings_;negativeImgOffsets=negativeImgOffsetsEnabled_;patchesEnabled=patchesEnabled_;refresh=std::exchange(refreshImagesRequested_,false);toggleRequested_=false;filterRequested_=false;}
            consumed_=generation;pendingNavigation.reset();playback.stop();if(audio)audio->stop();
            try{if(refresh||client!=activeClient||!assets){
                releaseReplay();assets.reset();if(!client.empty())assets=std::make_unique<Assets>(client/L"ImagePacks2",patchesEnabled);
                activeClient=client;
            }}catch(const std::exception& e){std::lock_guard lock(mutex_);if(generation_==generation){patchesEnabled_=status_.patchesEnabled;status_.phase=Phase::Error;status_.message=wide(e.what());updateAudio();}continue;}
            {std::lock_guard lock(mutex_);if(quitting_||generation_!=generation)continue;imageIndex_=assets?assets->index():Assets::IndexPtr{};if(assets)status_.patchesEnabled=assets->patchesEnabled();}
            if(stop){releaseAudio();std::lock_guard lock(mutex_);status_.phase=path.empty()?Phase::Empty:Phase::Stopped;status_.client=activeClient;updateAudio();}
            else try{
                if(inspection.valid())inspection.wait();
                if(path!=activeReplay){releaseReplay();executor=std::make_unique<Executor>(gpu,*assets,root_/L"runtime"/L"cache",client);}
                else if(!executor)executor=std::make_unique<Executor>(gpu,*assets,root_/L"runtime"/L"cache",client);
                auto options=clientReplayOptions(client,protocol_);
                auto opened=std::make_unique<Replay>(path,options);executor->setCanvasSettings(settings);executor->setNegativeImgOffsetsEnabled(negativeImgOffsets);executor->attach(*opened);executor->setHiddenImages(hidden);executor->setImageVisibility(visibility);executor->setTransparent(true);replay=std::move(opened);
                activeReplay=path;
                playback.attach(*replay,*executor);executor->prepare([&]{return quitting_||generation_!=generation;});
                if(quitting_||generation_!=generation)continue;
                initializeAudio(path,*replay,generation);
                if(quitting_||generation_!=generation)continue;
                {float volume;bool muted;{std::lock_guard lock(mutex_);volume=audioVolume_;muted=audioMuted_;}if(audio)audio->volume(volume,muted);}
                {double rate;{std::lock_guard lock(mutex_);rate=playbackRate_;}playback.setPlaybackRate(rate);if(audio)audio->setPlaybackRate(rate);}
                inspectedGeneration=generation;inspection=std::async(std::launch::async,[this,path,generation,options]{Replay scan(path,options);return inspectReplayImages(scan,[this,generation]{return quitting_||generation_!=generation;});});
                frozenPicture.reset();playback.start();startAudio(playback.elapsedMilliseconds());playingAt=Clock::now();auto before=Clock::now();playback.tick();present();auto after=Clock::now();
                frameTimes={std::chrono::duration<double,std::milli>(after-before).count()};
                {std::lock_guard lock(mutex_);status_.phase=Phase::Playing;status_.path=path.wstring();status_.client=activeClient;status_.readySeconds=std::chrono::duration<double>(after-requestedAt).count();status_.processReadySeconds=std::chrono::duration<double>(after-processStart_).count();status_.frames=1;status_.frameCount=0;status_.skipped=0;status_.ordinal=playback.ordinal();status_.timestamp=playback.timestamp();status_.elapsed=playback.elapsedMilliseconds();status_.width=replay->header.width();status_.height=replay->header.height();status_.maxFrameMilliseconds=frameTimes.back();status_.message.clear();status_.frozen=false;status_.compatibilityIgnored=replay->hasCompatibilityIgnored;status_.duration=0;updateImages();updateCanvas();updateAudio();}frozenChecked=false;
            }catch(const std::exception& e){releaseReplay();std::lock_guard lock(mutex_);status_.phase=Phase::Error;status_.message=wide(e.what());updateAudio();}
        }
        bool toggle,filter,capture,canvasChanged,audioSettings,muted,negativeImgOffsetsChanged,negativeImgOffsets,scrubChanged,scrubbing,rateChanged;double rate;float volume;std::unordered_set<std::string> hidden;CanvasSettings settings;ImageVisibilityPolicy visibility;std::optional<NavigationRequest> navigation;
        {std::lock_guard lock(mutex_);rateChanged=std::exchange(playbackRateRequested_,false);rate=playbackRate_;toggle=std::exchange(toggleRequested_,false);filter=std::exchange(filterRequested_,false);capture=std::exchange(captureRequested_,false);canvasChanged=std::exchange(canvasRequested_,false);audioSettings=std::exchange(audioSettingsRequested_,false);negativeImgOffsetsChanged=std::exchange(negativeImgOffsetsRequested_,false);negativeImgOffsets=negativeImgOffsetsEnabled_;muted=audioMuted_;volume=audioVolume_;scrubChanged=std::exchange(scrubChanged_,false);scrubbing=scrubbing_;hidden=hidden_;visibility=imageVisibility_;settings=canvasSettings_;
            if((filter||canvasChanged||negativeImgOffsetsChanged)&&!navigationRequested_&&!activeNavigation_)queueRefreshLocked();
            navigation=std::exchange(navigationRequested_,{});if(navigation)activeNavigation_=navigation;
        }
        if(audioSettings&&audio)audio->volume(volume,muted);
        if(rateChanged){playback.setPlaybackRate(rate);if(audio)audio->setPlaybackRate(rate);}
        if(replay&&executor)try{
            if(toggle){
                if(playback.playing()){playback.pause();if(audio)audio->pause();}
                else if(playback.paused()){playback.resume();startAudio(playback.elapsedMilliseconds());}
                else {initializeAudio(activeReplay,*replay,generation);if(audio){audio->volume(volume,muted);audio->setPlaybackRate(rate);}frozenPicture.reset();playback.start();startAudio(playback.elapsedMilliseconds());playingAt=Clock::now();}
                std::lock_guard lock(mutex_);status_.phase=playback.paused()?Phase::Paused:Phase::Playing;updateAudio();
            }
            if(inspection.valid()&&inspection.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){if(inspectedGeneration==generation){auto info=inspection.get();executor->setInspection(info);std::lock_guard lock(mutex_);status_.duration=info.durationMilliseconds;status_.frameCount=info.scenes;updateImages();}else inspection={};}
            if(scrubChanged&&scrubbing){auto elapsed=playback.elapsedMilliseconds();playback.finishNavigation(elapsed,false);if(audio)audio->pause();std::lock_guard lock(mutex_);if(scrubbing_){status_.elapsed=elapsed;status_.scrubbing=true;}}
            if(navigation){
                pendingNavigation.reset();auto request=*navigation;auto before=Clock::now();
                playback.finishNavigation(playback.elapsedMilliseconds(),false);if(audio)audio->pause();
                if(!frozenPicture){auto& output=executor->output();gpu.flush();frozenPicture=gpu.target(output.image.width,output.image.height,false);gpu.context()->CopyResource(frozenPicture->image.texture.get(),output.image.texture.get());}
                if(canvasChanged)executor->setCanvasSettings(settings);
                if(negativeImgOffsetsChanged)executor->setNegativeImgOffsetsEnabled(negativeImgOffsets);
                if(filter){executor->setHiddenImages(hidden);executor->setImageVisibility(visibility);}
                auto cancelled=[&]{return quitting_||generation_!=generation||navigationSerial_!=request.serial;};
                auto progress=[&](uint64_t ordinal){std::lock_guard lock(mutex_);if(navigationSerial_==request.serial&&generation_==generation)status_.navigationProgressOrdinal=ordinal;};
                if(request.resumeOnly&&!request.forceRebuild&&!filter&&!canvasChanged&&!negativeImgOffsetsChanged){
                    // Resume the frozen picture after the device has completed
                    // its seek; no scene-state rebuild is needed for playback.
                }else if(request.byOrdinal)playback.seekOrdinal(request.ordinal,cancelled,progress,request.forceRebuild);
                else {
                    if(request.forceRebuild)playback.seekOrdinal(0,cancelled,progress,true);
                    if(!cancelled())playback.seek(request.time,request.last,cancelled,progress);
                }
                if(!cancelled()){
                    if(request.preserveState)playback.setElapsedMilliseconds(request.time);
                    uint32_t pixelsCrc=test_?crc(gpu.readback(executor->output())):0;
                    // Validate and publish together, so a newer UI request cannot
                    // interleave an obsolete picture or audio seek submission.
                    std::lock_guard lock(mutex_);
                    if(generation_==generation&&navigationSerial_==request.serial){
                        present(true);
                        // A paused visual refresh leaves the frozen device clock.
                        auto audioSerial=audio&&(!request.preserveState||request.resume)?audio->seek(playback.elapsedMilliseconds(),false):0;
                        pendingNavigation=PendingNavigation{request,generation,audioSerial,playback.ended(),std::chrono::duration<double,std::milli>(Clock::now()-before).count(),pixelsCrc};
                    }
                }else {std::lock_guard lock(mutex_);if(activeNavigation_&&activeNavigation_->serial==request.serial)activeNavigation_.reset();}
            }
            if(pendingNavigation){
                auto& pending=*pendingNavigation;
                if(generation_!=pending.generation||navigationSerial_!=pending.request.serial)pendingNavigation.reset();
                else if(!audio||!audio->available()||audio->appliedSeekSerial()>=pending.audioSerial){
                    std::lock_guard lock(mutex_);
                    if(generation_==pending.generation&&navigationSerial_==pending.request.serial){
                        auto request=activeNavigation_.value_or(pending.request);
                        bool ended=request.last||pending.ended||(request.preserveState&&request.originalPhase==Phase::Ended);
                        bool stopped=request.preserveState&&request.originalPhase==Phase::Stopped;
                        bool resume=request.resume&&!ended&&!stopped;
                        if(audio&&audio->available()&&resume&&(pending.audioSerial==0||!audio->resumePrepared(pending.audioSerial))){
                            // Space may request resume after a paused visual
                            // rebuild began without repositioning audio.
                            pending.audioSerial=audio->seek(playback.elapsedMilliseconds(),false);
                            continue;
                        }
                        playback.finishNavigation(playback.elapsedMilliseconds(),resume,ended,stopped);
                        status_.phase=ended?Phase::Ended:stopped?Phase::Stopped:resume?Phase::Playing:Phase::Paused;
                        status_.scrubbing=false;status_.timestamp=playback.timestamp();status_.ordinal=playback.ordinal();status_.elapsed=playback.elapsedMilliseconds();status_.skipped=playback.skipped;
                        status_.frames++;if(test_){status_.frameCrc=pending.crc;status_.captureSerial++;}
                        status_.completedNavigationSerial=request.serial;status_.navigationProgressOrdinal=playback.ordinal();status_.maxFrameMilliseconds=std::max(status_.maxFrameMilliseconds,pending.milliseconds);status_.frozen=false;
                        updateCanvas();updateImages();updateAudio();activeNavigation_.reset();frozenChecked=false;
                    }
                    pendingNavigation.reset();
                }
            }
            bool seeking;{std::lock_guard lock(mutex_);seeking=status_.phase==Phase::Seeking;}
            if(seeking){std::unique_lock lock(mutex_);condition_.wait_for(lock,std::chrono::milliseconds(2),[&]{return quitting_||generation_!=consumed_||navigationRequested_||scrubChanged_||filterRequested_||canvasRequested_||negativeImgOffsetsRequested_;});continue;}
            // The device sample clock includes seek/start buffering and the
            // selected frequency ratio, so pictures follow the actual sound.
            // A reused device still holds the previous replay's clock until
            // the new start seek is applied. Keep its requested start meanwhile.
            if(playback.playing()&&audio&&audio->available())playback.setElapsedMilliseconds(audio->appliedSeekSerial()>=audioClockSerial?audio->positionMilliseconds():audioClockTarget);
            auto before=Clock::now();bool changed=playback.tick();RECT r;GetClientRect(canvas_,&r);bool resized=r.right!=oldWidth||r.bottom!=oldHeight;
            if(changed||resized)present();
            if(changed){double ms=std::chrono::duration<double,std::milli>(Clock::now()-before).count();frameTimes.push_back(ms);std::lock_guard lock(mutex_);status_.frames++;status_.skipped=playback.skipped;status_.timestamp=playback.timestamp();status_.ordinal=playback.ordinal();status_.maxFrameMilliseconds=std::max(status_.maxFrameMilliseconds,ms);updateImages();}
            {std::lock_guard lock(mutex_);status_.elapsed=playback.elapsedMilliseconds();if(status_.duration)status_.elapsed=std::min<int64_t>(status_.elapsed,status_.duration);updateAudio();}
            if(capture){auto pixels=gpu.readback(frozenPicture?*frozenPicture:executor->output());std::lock_guard lock(mutex_);status_.frameCrc=crc(pixels);status_.captureSerial++;}
            if(playback.ended()){
                bool newEnd=false;{std::lock_guard lock(mutex_);if(status_.phase==Phase::Playing){status_.phase=Phase::Ended;status_.playbackSeconds=std::chrono::duration<double>(Clock::now()-playingAt).count();status_.duration=std::max(status_.duration,status_.timestamp);status_.elapsed=status_.duration;newEnd=true;}}
                if(newEnd){if(audio)audio->seek(playback.timestamp(),false);{std::lock_guard lock(mutex_);updateAudio();}endedAt=Clock::now();if(test_){auto pixels=gpu.readback(executor->output());finalCrc=crc(pixels);std::ofstream out(root_/L"validation"/L"ui_last_frame.rgba",std::ios::binary);out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());std::sort(frameTimes.begin(),frameTimes.end());auto percentile=[&](double p){return frameTimes[std::min(frameTimes.size()-1,size_t(p*(frameTimes.size()-1)))];};std::lock_guard lock(mutex_);status_.p50Frame=percentile(.50);status_.p95Frame=percentile(.95);status_.p99Frame=percentile(.99);}}
                if(test_&&!frozenChecked&&Clock::now()-endedAt>std::chrono::milliseconds(800)){bool same=crc(gpu.readback(executor->output()))==finalCrc;std::lock_guard lock(mutex_);status_.frozen=same;status_.finalCrc=finalCrc;frozenChecked=true;}
            }
        }catch(const std::exception& e){releaseReplay();std::lock_guard lock(mutex_);status_.phase=Phase::Error;status_.message=wide(e.what());updateAudio();}
        std::unique_lock lock(mutex_);condition_.wait_for(lock,std::chrono::milliseconds(playback.playing()||pendingNavigation?1:20),[&]{return quitting_||generation_!=consumed_||toggleRequested_||filterRequested_||captureRequested_||canvasRequested_||audioSettingsRequested_||playbackRateRequested_||negativeImgOffsetsRequested_||scrubChanged_||navigationRequested_;});
    }
    if(inspection.valid())inspection.wait();gpu.flush();
}catch(const std::exception& e){std::lock_guard lock(mutex_);status_.phase=Phase::Error;status_.message=wide(e.what());status_.audioAvailable=false;status_.audioEvents=status_.missingSoundCount=0;status_.audioPosition=0;status_.audioMessage.clear();}}
}
