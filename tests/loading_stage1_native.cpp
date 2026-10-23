#include "ui_playback.hpp"
#include "loading_measure.hpp"
#include <iostream>
#include <thread>

std::mutex measureMutex;
std::condition_variable measureCondition;
bool measureAudioEntered=false,measureOverlap=false,measureInspectEntered=false,measureReleaseInspect=false;
bool measureImageFailed=false,measureAudioCancelled=false,measureReleaseAudio=false;
std::string measureMode;
void measureImagePreparation(){
    if(measureMode!="parallel"&&measureMode!="error_cancel")return;
    std::unique_lock lock(measureMutex);
    measureOverlap=measureCondition.wait_for(lock,std::chrono::milliseconds(400),[]{return measureAudioEntered;});
    if(measureMode=="error_cancel"){
        if(!measureOverlap)throw rep::Error("Audio preparation did not enter before injected image failure");
        measureImageFailed=true;measureCondition.notify_all();
        throw rep::Error("Injected image preparation failure");
    }
}
void measureAudioPreparation(const std::function<bool()>& cancelled){
    if(measureMode!="parallel"&&measureMode!="error_cancel")return;
    std::unique_lock lock(measureMutex);measureAudioEntered=true;measureCondition.notify_all();
    while(measureMode=="error_cancel"&&!measureReleaseAudio){
        if(cancelled&&cancelled()){measureAudioCancelled=true;measureCondition.notify_all();return;}
        measureCondition.wait_for(lock,std::chrono::milliseconds(2));
    }
}
void measureInspection(){
    if(measureMode!="cancel")return;
    std::unique_lock lock(measureMutex);
    if(measureInspectEntered)return;
    measureInspectEntered=true;measureCondition.notify_all();
    measureCondition.wait(lock,[]{return measureReleaseInspect;});
}
static void pump(){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
template<class Predicate> bool wait(rep::ui::PlaybackController& player,Predicate predicate,int milliseconds=15000){
    auto end=rep::ui::Clock::now()+std::chrono::milliseconds(milliseconds);
    while(rep::ui::Clock::now()<end){auto s=player.status();if(s.phase==rep::ui::Phase::Error)throw rep::Error(rep::utf8(s.message));if(predicate(s))return true;pump();}return false;
}
int wmain(int argc,wchar_t** argv){
    if(argc!=4)return 2;measureMode=rep::utf8(argv[1]);HWND window=nullptr;
    try{
        std::filesystem::path root=argv[2],client=argv[3];
        if(measureMode=="inspect_cancel"){
            rep::Replay source(client/L"Replay"/L"heavy.rep");unsigned checks=0;
            auto info=rep::inspectReplayImages(source,[&]{return ++checks>=2;});uint64_t references=0;for(const auto& call:info.images)references+=call.timelineCount;
            bool passed=checks>=2&&references<5000;
            std::cout<<"{\"pass\":"<<(passed?"true":"false")<<"}\n";return passed?0:1;
        }
        window=CreateWindowW(L"STATIC",L"Loading stage one regression",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window)throw rep::Error("Cannot create owned window");
        bool passed=false,errorPublished=false,audioCancelled=false;double errorMilliseconds=0;
        {
            rep::ui::PlaybackController player(window,root,client,false,rep::ui::Clock::now());player.setAudioMuted(true);
            player.open(client/L"Replay"/L"a.rep");
            if(measureMode=="error_cancel"){
                {std::unique_lock lock(measureMutex);if(!measureCondition.wait_for(lock,std::chrono::seconds(5),[]{return measureImageFailed;}))throw rep::Error("Image failure boundary did not execute");}
                auto before=rep::ui::Clock::now(),deadline=before+std::chrono::milliseconds(600);
                while(rep::ui::Clock::now()<deadline){
                    auto status=player.status();
                    if(status.phase==rep::ui::Phase::Error){errorPublished=status.message==L"Injected image preparation failure";break;}
                    pump();
                }
                errorMilliseconds=std::chrono::duration<double,std::milli>(rep::ui::Clock::now()-before).count();
                {std::lock_guard lock(measureMutex);audioCancelled=measureAudioCancelled;measureReleaseAudio=true;measureCondition.notify_all();}
                passed=errorPublished&&audioCancelled;
            }else{
                if(!wait(player,[](const auto& s){return s.phase==rep::ui::Phase::Playing;}))throw rep::Error("First open timed out");
                if(measureMode=="parallel")passed=measureOverlap&&player.status().audioEvents==1;
                else if(measureMode=="cancel"){
                    {std::unique_lock lock(measureMutex);if(!measureCondition.wait_for(lock,std::chrono::seconds(3),[]{return measureInspectEntered;}))throw rep::Error("Inspection did not start");}
                    player.open(client/L"Replay"/L"b.rep");
                    passed=wait(player,[&](const auto& s){return s.phase==rep::ui::Phase::Playing&&s.path==(client/L"Replay"/L"b.rep").wstring();},350);
                    {std::lock_guard lock(measureMutex);measureReleaseInspect=true;measureCondition.notify_all();}
                    if(!wait(player,[](const auto& s){return s.phase==rep::ui::Phase::Playing&&s.frameCount==2;}))throw rep::Error("Latest inspection not published");
                }else throw rep::Error("Unknown mode");
            }
        }
        DestroyWindow(window);std::cout<<"{\"pass\":"<<(passed?"true":"false");
        if(measureMode=="error_cancel")std::cout<<",\"error_published\":"<<(errorPublished?"true":"false")<<",\"audio_cancelled\":"<<(audioCancelled?"true":"false")<<",\"error_ms\":"<<errorMilliseconds;
        std::cout<<"}\n";return passed?0:1;
    }catch(const std::exception& error){
        {std::lock_guard lock(measureMutex);measureReleaseInspect=measureReleaseAudio=true;measureCondition.notify_all();}
        if(window)DestroyWindow(window);std::cerr<<error.what()<<'\n';return 2;
    }
}
