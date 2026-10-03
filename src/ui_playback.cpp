#include "ui_playback.hpp"
#include <algorithm>
#include <fstream>
#include <future>
#include <utility>

namespace rep::ui {
PlaybackController::PlaybackController(HWND canvas,std::filesystem::path root,std::filesystem::path client,bool test,Clock::time_point processStart)
    :canvas_(canvas),root_(std::move(root)),client_(std::move(client)),test_(test),processStart_(processStart) {
    thread_=std::thread([this]{run();});
}
PlaybackController::~PlaybackController(){quitting_=true;generation_++;condition_.notify_all();if(thread_.joinable())thread_.join();}
void PlaybackController::open(const std::filesystem::path& path){std::lock_guard lock(mutex_);requested_=path;requestedAt_=Clock::now();stopRequested_=false;stepsRequested_.clear();hidden_.clear();status_.frameCount=0;status_.hiddenImages.clear();status_.currentImages.clear();status_.allImages.clear();status_.phase=Phase::Loading;status_.message.clear();status_.path=path.wstring();generation_++;condition_.notify_all();}
void PlaybackController::replay(){auto s=status();if(s.path.empty())return;std::lock_guard lock(mutex_);requested_=s.path;requestedAt_=Clock::now();stopRequested_=false;stepsRequested_.clear();status_.phase=Phase::Loading;generation_++;condition_.notify_all();}
void PlaybackController::stop(){std::lock_guard lock(mutex_);stopRequested_=true;stepsRequested_.clear();generation_++;condition_.notify_all();}
void PlaybackController::togglePause(){std::lock_guard lock(mutex_);if(status_.path.empty())return;toggleRequested_=true;condition_.notify_all();}
void PlaybackController::stepFrame(int direction){if(!direction)return;std::lock_guard lock(mutex_);if(status_.path.empty()||status_.phase==Phase::Loading||status_.phase==Phase::Error)return;stepsRequested_.push_back(direction<0?-1:1);condition_.notify_all();}
void PlaybackController::configureClient(const std::filesystem::path& client){std::lock_guard lock(mutex_);client_=client;requested_.clear();hidden_.clear();stepsRequested_.clear();stopRequested_=true;toggleRequested_=filterRequested_=captureRequested_=false;status_=PlayerStatus{};status_.client=client_;generation_++;condition_.notify_all();}
void PlaybackController::setHiddenImages(std::unordered_set<std::string> hidden){std::lock_guard lock(mutex_);hidden_=std::move(hidden);status_.hiddenImages=hidden_;if(status_.path.empty())return;filterRequested_=true;condition_.notify_all();}
void PlaybackController::captureFrame(){std::lock_guard lock(mutex_);if(status_.path.empty())return;captureRequested_=true;condition_.notify_all();}
PlayerStatus PlaybackController::status(){std::lock_guard lock(mutex_);return status_;}

void PlaybackController::run(){try{
    Gpu gpu(root_/L"assets"/L"shaders",canvas_);gpu.createAllPrograms();
    std::unique_ptr<Assets> assets;std::unique_ptr<Executor> executor;std::unique_ptr<Replay> replay;
    Playback playback;std::filesystem::path activeClient;
    std::future<ReplayInspection> inspection;uint64_t inspectedGeneration=0;
    Clock::time_point endedAt,playingAt;bool frozenChecked=false;uint32_t finalCrc=0;int oldWidth=0,oldHeight=0;
    std::vector<double> frameTimes;
    auto updateImages=[&]{if(!executor)return;status_.currentImages=executor->currentImages();status_.allImages=executor->allImages();status_.hiddenImages=executor->hiddenImages();};
    auto present=[&]{RECT r;GetClientRect(canvas_,&r);gpu.present(executor->output(),std::max(1L,r.right),std::max(1L,r.bottom));oldWidth=r.right;oldHeight=r.bottom;};
    while(!quitting_){
        uint64_t generation=generation_;
        if(generation!=consumed_){
            std::filesystem::path path,client;Clock::time_point requestedAt;bool stop;std::unordered_set<std::string> hidden;
            {std::lock_guard lock(mutex_);path=requested_;client=client_;requestedAt=requestedAt_;stop=stopRequested_;hidden=hidden_;toggleRequested_=false;filterRequested_=false;}
            consumed_=generation;playback.stop();
            try{if(client!=activeClient){
                replay.reset();executor.reset();assets.reset();assets=std::make_unique<Assets>(client/L"ImagePacks2");
                gpu.resetTextures();
                executor=std::make_unique<Executor>(gpu,*assets,root_/L"runtime"/L"cache",client);activeClient=client;
            }}catch(const std::exception& e){std::lock_guard lock(mutex_);status_.phase=Phase::Error;status_.message=wide(e.what());continue;}
            if(stop){std::lock_guard lock(mutex_);status_.phase=path.empty()?Phase::Empty:Phase::Stopped;status_.client=activeClient;}
            else try{
                if(inspection.valid())inspection.wait();
                auto opened=std::make_unique<Replay>(path);executor->attach(*opened);executor->setHiddenImages(hidden);executor->setTransparent(true);replay=std::move(opened);
                playback.attach(*replay,*executor);executor->prepare([&]{return quitting_||generation_!=generation;});
                if(quitting_||generation_!=generation)continue;
                inspectedGeneration=generation;inspection=std::async(std::launch::async,[this,path,generation]{Replay scan(path);return inspectReplayImages(scan,[this,generation]{return quitting_||generation_!=generation;});});
                playback.start();playingAt=Clock::now();auto before=Clock::now();playback.tick();present();auto after=Clock::now();
                frameTimes={std::chrono::duration<double,std::milli>(after-before).count()};
                {std::lock_guard lock(mutex_);status_.phase=Phase::Playing;status_.path=path.wstring();status_.client=activeClient;status_.readySeconds=std::chrono::duration<double>(after-requestedAt).count();status_.processReadySeconds=std::chrono::duration<double>(after-processStart_).count();status_.frames=1;status_.frameCount=0;status_.skipped=0;status_.ordinal=playback.ordinal();status_.timestamp=playback.timestamp();status_.elapsed=playback.elapsedMilliseconds();status_.width=replay->header.width();status_.height=replay->header.height();status_.maxFrameMilliseconds=frameTimes.back();status_.message.clear();status_.frozen=false;status_.duration=0;updateImages();}frozenChecked=false;
            }catch(const std::exception& e){playback.stop();playback=Playback{};replay.reset();std::lock_guard lock(mutex_);status_.phase=Phase::Error;status_.message=wide(e.what());}
        }
        bool toggle,filter,capture;std::unordered_set<std::string> hidden;std::vector<int> steps;
        {std::lock_guard lock(mutex_);toggle=std::exchange(toggleRequested_,false);filter=std::exchange(filterRequested_,false);capture=std::exchange(captureRequested_,false);steps=std::exchange(stepsRequested_,{});hidden=hidden_;}
        if(replay&&executor)try{
            if(toggle){if(playback.ended()){playback.start();playingAt=Clock::now();}else if(playback.paused())playback.resume();else if(playback.playing())playback.pause();else {playback.start();playingAt=Clock::now();}std::lock_guard lock(mutex_);status_.phase=playback.paused()?Phase::Paused:Phase::Playing;}
            if(inspection.valid()&&inspection.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){if(inspectedGeneration==generation){auto info=inspection.get();executor->setInspection(info);std::lock_guard lock(mutex_);status_.duration=info.durationMilliseconds;status_.frameCount=info.scenes;updateImages();}else inspection={};}
            if(filter){executor->setHiddenImages(hidden);playback.refresh();present();std::lock_guard lock(mutex_);updateImages();}
            for(int direction:steps){auto before=Clock::now();bool moved=playback.step(direction);present();auto pixels=gpu.readback(executor->output());std::lock_guard lock(mutex_);status_.phase=Phase::Paused;status_.timestamp=playback.timestamp();status_.ordinal=playback.ordinal();status_.elapsed=playback.elapsedMilliseconds();status_.skipped=playback.skipped;if(moved)status_.frames++;status_.frameCrc=crc(pixels);status_.captureSerial++;status_.maxFrameMilliseconds=std::max(status_.maxFrameMilliseconds,std::chrono::duration<double,std::milli>(Clock::now()-before).count());status_.frozen=false;updateImages();frozenChecked=false;}
            auto before=Clock::now();bool changed=playback.tick();RECT r;GetClientRect(canvas_,&r);bool resized=r.right!=oldWidth||r.bottom!=oldHeight;
            if(changed||resized)present();
            if(changed){double ms=std::chrono::duration<double,std::milli>(Clock::now()-before).count();frameTimes.push_back(ms);std::lock_guard lock(mutex_);status_.frames++;status_.skipped=playback.skipped;status_.timestamp=playback.timestamp();status_.ordinal=playback.ordinal();status_.maxFrameMilliseconds=std::max(status_.maxFrameMilliseconds,ms);updateImages();}
            {std::lock_guard lock(mutex_);status_.elapsed=playback.elapsedMilliseconds();if(status_.duration)status_.elapsed=std::min<int64_t>(status_.elapsed,status_.duration);}
            if(capture){auto pixels=gpu.readback(executor->output());std::lock_guard lock(mutex_);status_.frameCrc=crc(pixels);status_.captureSerial++;}
            if(playback.ended()){
                bool newEnd=false;{std::lock_guard lock(mutex_);if(status_.phase==Phase::Playing){status_.phase=Phase::Ended;status_.playbackSeconds=std::chrono::duration<double>(Clock::now()-playingAt).count();status_.duration=std::max(status_.duration,status_.timestamp);status_.elapsed=status_.duration;newEnd=true;}}
                if(newEnd){endedAt=Clock::now();if(test_){auto pixels=gpu.readback(executor->output());finalCrc=crc(pixels);std::ofstream out(root_/L"validation"/L"ui_last_frame.rgba",std::ios::binary);out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());std::sort(frameTimes.begin(),frameTimes.end());auto percentile=[&](double p){return frameTimes[std::min(frameTimes.size()-1,size_t(p*(frameTimes.size()-1)))];};std::lock_guard lock(mutex_);status_.p50Frame=percentile(.50);status_.p95Frame=percentile(.95);status_.p99Frame=percentile(.99);}}
                if(test_&&!frozenChecked&&Clock::now()-endedAt>std::chrono::milliseconds(800)){bool same=crc(gpu.readback(executor->output()))==finalCrc;std::lock_guard lock(mutex_);status_.frozen=same;status_.finalCrc=finalCrc;frozenChecked=true;}
            }
        }catch(const std::exception& e){playback.stop();playback=Playback{};replay.reset();std::lock_guard lock(mutex_);status_.phase=Phase::Error;status_.message=wide(e.what());}
        std::unique_lock lock(mutex_);condition_.wait_for(lock,std::chrono::milliseconds(playback.playing()?1:20),[&]{return quitting_||generation_!=consumed_||toggleRequested_||filterRequested_||captureRequested_||!stepsRequested_.empty();});
    }
    if(inspection.valid())inspection.wait();gpu.flush();
}catch(const std::exception& e){std::lock_guard lock(mutex_);status_.phase=Phase::Error;status_.message=wide(e.what());}}
}
