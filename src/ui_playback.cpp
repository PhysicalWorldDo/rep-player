#include "ui_playback.hpp"
#include "audio.hpp"
#include <algorithm>
#include <fstream>
#include <future>
#include <utility>

namespace rep::ui {
PlaybackController::PlaybackController(HWND canvas,std::filesystem::path root,std::filesystem::path client,bool test,Clock::time_point processStart,ClientProtocolSelection protocol,CanvasSettings settings)
    :canvas_(canvas),root_(std::move(root)),client_(std::move(client)),protocol_(std::move(protocol)),canvasSettings_(std::move(settings)),test_(test),processStart_(processStart) {
    thread_=std::thread([this]{run();});
}
PlaybackController::~PlaybackController(){quitting_=true;generation_++;condition_.notify_all();if(thread_.joinable())thread_.join();}
void PlaybackController::open(const std::filesystem::path& path){std::lock_guard lock(mutex_);requested_=path;requestedAt_=Clock::now();stopRequested_=false;stepsRequested_.clear();hidden_.clear();status_.frameCount=0;status_.hiddenImages.clear();status_.currentImages.clear();status_.allImages.clear();status_.phase=Phase::Loading;status_.compatibilityIgnored=false;status_.message.clear();status_.path=path.wstring();generation_++;condition_.notify_all();}
void PlaybackController::replay(){auto s=status();if(s.path.empty())return;std::lock_guard lock(mutex_);requested_=s.path;requestedAt_=Clock::now();stopRequested_=false;stepsRequested_.clear();status_.phase=Phase::Loading;generation_++;condition_.notify_all();}
void PlaybackController::stop(){std::lock_guard lock(mutex_);stopRequested_=true;stepsRequested_.clear();generation_++;condition_.notify_all();}
void PlaybackController::togglePause(){std::lock_guard lock(mutex_);if(status_.path.empty())return;toggleRequested_=true;condition_.notify_all();}
void PlaybackController::stepFrame(int direction){if(!direction)return;std::lock_guard lock(mutex_);if(status_.path.empty()||status_.phase==Phase::Loading||status_.phase==Phase::Error)return;stepsRequested_.push_back(direction<0?-1:1);condition_.notify_all();}
void PlaybackController::configureClient(const std::filesystem::path& client){std::lock_guard lock(mutex_);client_=client;requested_.clear();hidden_.clear();stepsRequested_.clear();stopRequested_=true;toggleRequested_=filterRequested_=captureRequested_=false;status_=PlayerStatus{};status_.client=client_;status_.audioVolume=audioVolume_;status_.audioMuted=audioMuted_;generation_++;condition_.notify_all();}
void PlaybackController::setHiddenImages(std::unordered_set<std::string> hidden){std::lock_guard lock(mutex_);hidden_=std::move(hidden);status_.hiddenImages=hidden_;if(status_.path.empty())return;filterRequested_=true;condition_.notify_all();}
void PlaybackController::setCanvasSettings(const CanvasSettings& settings){std::lock_guard lock(mutex_);canvasSettings_=settings;canvasRequested_=true;condition_.notify_all();}
void PlaybackController::setAudioVolume(float volume){std::lock_guard lock(mutex_);audioVolume_=std::clamp(volume,0.f,1.f);status_.audioVolume=audioVolume_;audioSettingsRequested_=true;condition_.notify_all();}
void PlaybackController::setAudioMuted(bool muted){std::lock_guard lock(mutex_);audioMuted_=muted;status_.audioMuted=muted;audioSettingsRequested_=true;condition_.notify_all();}
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
    auto releaseAudio=[&]{if(audio)audio->stop();audio.reset();audioEpoch.reset();audioMessage.clear();};
    auto releaseReplay=[&]{releaseAudio();gpu.flush();playback=Playback{};replay.reset();executor.reset();gpu.resetTextures();if(assets)assets->clearDecodedImages();activeReplay.clear();};
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
    auto present=[&]{RECT r;GetClientRect(canvas_,&r);gpu.present(executor->output(),std::max(1L,r.right),std::max(1L,r.bottom));oldWidth=r.right;oldHeight=r.bottom;};
    while(!quitting_){
        uint64_t generation=generation_;
        if(generation!=consumed_){
            std::filesystem::path path,client;Clock::time_point requestedAt;bool stop;std::unordered_set<std::string> hidden;CanvasSettings settings;
            {std::lock_guard lock(mutex_);path=requested_;client=client_;requestedAt=requestedAt_;stop=stopRequested_;hidden=hidden_;settings=canvasSettings_;toggleRequested_=false;filterRequested_=false;}
            consumed_=generation;playback.stop();if(audio)audio->stop();
            try{if(client!=activeClient){
                releaseReplay();assets.reset();assets=std::make_unique<Assets>(client/L"ImagePacks2");
                activeClient=client;
            }}catch(const std::exception& e){std::lock_guard lock(mutex_);status_.phase=Phase::Error;status_.message=wide(e.what());updateAudio();continue;}
            if(stop){releaseAudio();std::lock_guard lock(mutex_);status_.phase=path.empty()?Phase::Empty:Phase::Stopped;status_.client=activeClient;updateAudio();}
            else try{
                if(inspection.valid())inspection.wait();
                if(path!=activeReplay){releaseReplay();executor=std::make_unique<Executor>(gpu,*assets,root_/L"runtime"/L"cache",client);}
                else if(!executor)executor=std::make_unique<Executor>(gpu,*assets,root_/L"runtime"/L"cache",client);
                auto options=clientReplayOptions(client,protocol_);
                auto opened=std::make_unique<Replay>(path,options);executor->setCanvasSettings(settings);executor->attach(*opened);executor->setHiddenImages(hidden);executor->setTransparent(true);replay=std::move(opened);
                activeReplay=path;
                playback.attach(*replay,*executor);executor->prepare([&]{return quitting_||generation_!=generation;});
                if(quitting_||generation_!=generation)continue;
                initializeAudio(path,*replay,generation);
                if(quitting_||generation_!=generation)continue;
                {float volume;bool muted;{std::lock_guard lock(mutex_);volume=audioVolume_;muted=audioMuted_;}if(audio)audio->volume(volume,muted);}
                inspectedGeneration=generation;inspection=std::async(std::launch::async,[this,path,generation,options]{Replay scan(path,options);return inspectReplayImages(scan,[this,generation]{return quitting_||generation_!=generation;});});
                playback.start();if(audio)audio->play(playback.elapsedMilliseconds());playingAt=Clock::now();auto before=Clock::now();playback.tick();present();auto after=Clock::now();
                frameTimes={std::chrono::duration<double,std::milli>(after-before).count()};
                {std::lock_guard lock(mutex_);status_.phase=Phase::Playing;status_.path=path.wstring();status_.client=activeClient;status_.readySeconds=std::chrono::duration<double>(after-requestedAt).count();status_.processReadySeconds=std::chrono::duration<double>(after-processStart_).count();status_.frames=1;status_.frameCount=0;status_.skipped=0;status_.ordinal=playback.ordinal();status_.timestamp=playback.timestamp();status_.elapsed=playback.elapsedMilliseconds();status_.width=replay->header.width();status_.height=replay->header.height();status_.maxFrameMilliseconds=frameTimes.back();status_.message.clear();status_.frozen=false;status_.compatibilityIgnored=replay->hasCompatibilityIgnored;status_.duration=0;updateImages();updateCanvas();updateAudio();}frozenChecked=false;
            }catch(const std::exception& e){releaseReplay();std::lock_guard lock(mutex_);status_.phase=Phase::Error;status_.message=wide(e.what());updateAudio();}
        }
        bool toggle,filter,capture,canvasChanged,audioSettings,muted;float volume;std::unordered_set<std::string> hidden;std::vector<int> steps;CanvasSettings settings;
        {std::lock_guard lock(mutex_);toggle=std::exchange(toggleRequested_,false);filter=std::exchange(filterRequested_,false);capture=std::exchange(captureRequested_,false);canvasChanged=std::exchange(canvasRequested_,false);audioSettings=std::exchange(audioSettingsRequested_,false);muted=audioMuted_;volume=audioVolume_;steps=std::exchange(stepsRequested_,{});hidden=hidden_;settings=canvasSettings_;}
        if(audioSettings&&audio)audio->volume(volume,muted);
        if(replay&&executor)try{
            if(toggle){
                if(playback.playing()){playback.pause();if(audio)audio->pause();}
                else if(playback.paused()){playback.resume();if(audio)audio->resume(playback.elapsedMilliseconds());}
                else {initializeAudio(activeReplay,*replay,generation);if(audio)audio->volume(volume,muted);playback.start();if(audio)audio->play(playback.elapsedMilliseconds());playingAt=Clock::now();}
                std::lock_guard lock(mutex_);status_.phase=playback.paused()?Phase::Paused:Phase::Playing;updateAudio();
            }
            if(inspection.valid()&&inspection.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){if(inspectedGeneration==generation){auto info=inspection.get();executor->setInspection(info);std::lock_guard lock(mutex_);status_.duration=info.durationMilliseconds;status_.frameCount=info.scenes;updateImages();}else inspection={};}
            if(canvasChanged){playback.setCanvasSettings(settings);present();std::lock_guard lock(mutex_);updateCanvas();updateImages();}
            if(filter){executor->setHiddenImages(hidden);playback.refresh();present();std::lock_guard lock(mutex_);updateImages();}
            for(int direction:steps){auto before=Clock::now();bool moved=playback.step(direction);if(audio)audio->seek(playback.elapsedMilliseconds(),false);present();auto pixels=gpu.readback(executor->output());std::lock_guard lock(mutex_);status_.phase=Phase::Paused;status_.timestamp=playback.timestamp();status_.ordinal=playback.ordinal();status_.elapsed=playback.elapsedMilliseconds();status_.skipped=playback.skipped;if(moved)status_.frames++;status_.frameCrc=crc(pixels);status_.captureSerial++;status_.maxFrameMilliseconds=std::max(status_.maxFrameMilliseconds,std::chrono::duration<double,std::milli>(Clock::now()-before).count());status_.frozen=false;updateImages();updateAudio();frozenChecked=false;}
            auto before=Clock::now();bool changed=playback.tick();RECT r;GetClientRect(canvas_,&r);bool resized=r.right!=oldWidth||r.bottom!=oldHeight;
            if(changed||resized)present();
            if(changed){double ms=std::chrono::duration<double,std::milli>(Clock::now()-before).count();frameTimes.push_back(ms);std::lock_guard lock(mutex_);status_.frames++;status_.skipped=playback.skipped;status_.timestamp=playback.timestamp();status_.ordinal=playback.ordinal();status_.maxFrameMilliseconds=std::max(status_.maxFrameMilliseconds,ms);updateImages();}
            {std::lock_guard lock(mutex_);status_.elapsed=playback.elapsedMilliseconds();if(status_.duration)status_.elapsed=std::min<int64_t>(status_.elapsed,status_.duration);updateAudio();}
            if(capture){auto pixels=gpu.readback(executor->output());std::lock_guard lock(mutex_);status_.frameCrc=crc(pixels);status_.captureSerial++;}
            if(playback.ended()){
                bool newEnd=false;{std::lock_guard lock(mutex_);if(status_.phase==Phase::Playing){status_.phase=Phase::Ended;status_.playbackSeconds=std::chrono::duration<double>(Clock::now()-playingAt).count();status_.duration=std::max(status_.duration,status_.timestamp);status_.elapsed=status_.duration;newEnd=true;}}
                if(newEnd){if(audio)audio->seek(playback.timestamp(),false);{std::lock_guard lock(mutex_);updateAudio();}endedAt=Clock::now();if(test_){auto pixels=gpu.readback(executor->output());finalCrc=crc(pixels);std::ofstream out(root_/L"validation"/L"ui_last_frame.rgba",std::ios::binary);out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());std::sort(frameTimes.begin(),frameTimes.end());auto percentile=[&](double p){return frameTimes[std::min(frameTimes.size()-1,size_t(p*(frameTimes.size()-1)))];};std::lock_guard lock(mutex_);status_.p50Frame=percentile(.50);status_.p95Frame=percentile(.95);status_.p99Frame=percentile(.99);}}
                if(test_&&!frozenChecked&&Clock::now()-endedAt>std::chrono::milliseconds(800)){bool same=crc(gpu.readback(executor->output()))==finalCrc;std::lock_guard lock(mutex_);status_.frozen=same;status_.finalCrc=finalCrc;frozenChecked=true;}
            }
        }catch(const std::exception& e){releaseReplay();std::lock_guard lock(mutex_);status_.phase=Phase::Error;status_.message=wide(e.what());updateAudio();}
        std::unique_lock lock(mutex_);condition_.wait_for(lock,std::chrono::milliseconds(playback.playing()?1:20),[&]{return quitting_||generation_!=consumed_||toggleRequested_||filterRequested_||captureRequested_||canvasRequested_||audioSettingsRequested_||!stepsRequested_.empty();});
    }
    if(inspection.valid())inspection.wait();gpu.flush();
}catch(const std::exception& e){std::lock_guard lock(mutex_);status_.phase=Phase::Error;status_.message=wide(e.what());status_.audioAvailable=false;status_.audioEvents=status_.missingSoundCount=0;status_.audioPosition=0;status_.audioMessage.clear();}}
}
