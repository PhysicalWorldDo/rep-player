#include "audio_resources.hpp"
#include "ui_playback.hpp"
#include "export.hpp"
#include <fstream>
#include <iostream>
#include <thread>

template<class T> std::shared_ptr<T> resources(const std::filesystem::path& client,const std::filesystem::path& cache,bool enabled){
    if constexpr(std::is_constructible_v<T,std::filesystem::path,std::filesystem::path,bool>)return std::make_shared<T>(client,cache,enabled);
    else return std::make_shared<T>(client,cache);
}
template<class T> rep::AudioResources::IndexPtr appliedAudio(T& player){
    if constexpr(requires {player.audioIndex();})return player.audioIndex();
    else return {};
}
template<class T> void exportAudio(T& options,rep::AudioResources::IndexPtr audio){
    if constexpr(requires {options.audioIndex=audio;})options.audioIndex=std::move(audio);
}
static float sample(rep::AudioResources& audio,const std::string& tag="TONE"){
    auto clip=audio.clip(tag);return clip&&!clip->samples.empty()?clip->samples.front():-9;
}
static float sample(const rep::AudioResources::IndexPtr& index){rep::AudioResources audio(index);return sample(audio);}
static void wait(rep::ui::PlaybackController& player,rep::ui::Phase phase){
    auto deadline=rep::ui::Clock::now()+std::chrono::seconds(20);
    while(rep::ui::Clock::now()<deadline){
        auto s=player.status();if(s.phase==rep::ui::Phase::Error)throw rep::Error(rep::utf8(s.message));
        if(s.phase==phase)return;std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }throw rep::Error("sound patch controller timeout");
}
int wmain(int argc,wchar_t** argv){try{
    if(argc<4)return 2;std::wstring mode=argv[1];std::filesystem::path client=argv[2],root=argv[3];
    if(mode==L"resources"||mode==L"pcm"){
        auto audio=resources<rep::AudioResources>(client,root,argc<5||std::wstring_view(argv[4])==L"1");
        if(mode==L"pcm"){
            if(argc<6)return 2;auto clip=audio->clip(rep::utf8(argv[5]));if(!clip)throw rep::Error("real sound clip missing");
            std::ofstream output(root/L"decoded.f32",std::ios::binary);output.write(reinterpret_cast<const char*>(clip->samples.data()),clip->samples.size()*sizeof(float));
            std::cout<<"{\"frames\":"<<clip->frames()<<",\"missing\":"<<audio->missing()<<"}";return 0;
        }
        auto tone=sample(*audio),other=sample(*audio,"OTHER");
        std::cout<<"{\"tone\":"<<tone<<",\"other\":"<<other<<",\"missing\":"<<audio->missing()<<"}";return 0;
    }
    HWND window=CreateWindowW(L"STATIC",L"Owned sound patch test",WS_OVERLAPPEDWINDOW,0,0,100,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)throw rep::Error("cannot create test canvas");
    struct Window {HWND h;~Window(){DestroyWindow(h);}} owned{window};
    rep::ui::PlaybackController player(window,root,client,false,rep::ui::Clock::now());player.setAudioMuted(true);
    player.refreshImages(true);wait(player,rep::ui::Phase::Empty);auto audio=appliedAudio(player);
    if(!audio){std::cout<<"{\"supported\":false}";return 0;}
    auto enabled=sample(audio);auto originalIndex=player.imageIndex();
    if(mode==L"fallback")std::filesystem::copy_file(root/L"repaired.npk",client/L"SoundPacks"/L"%patch.npk",std::filesystem::copy_options::overwrite_existing);
    else std::filesystem::copy_file(root/L"added.npk",client/L"SoundPacks"/L"!added.npk");
    player.open(client/L"Replay"/L"missing.rep");wait(player,rep::ui::Phase::Ended);
    auto replay=client/L"Replay"/L"tone.rep";player.open(replay);wait(player,rep::ui::Phase::Ended);
    bool clean=player.status().missingSoundCount==0&&player.status().audioMessage.empty();
    bool same=appliedAudio(player)==audio&&player.imageIndex()==originalIndex;
    player.replay();wait(player,rep::ui::Phase::Ended);same=same&&appliedAudio(player)==audio;
    player.stepFrame(-1);wait(player,rep::ui::Phase::Paused);same=same&&appliedAudio(player)==audio;
    // Export the applied sources after a new earlier patch has appeared on disk.
    rep::Gpu gpu(root/L"assets"/L"shaders");gpu.createAllPrograms();rep::Assets assets(originalIndex);
    rep::ExportOptions options;options.fps=30;options.outputDirectory=root;options.fileName=L"applied";exportAudio(options,audio);
    auto exported=rep::exportReplay(gpu,assets,client,root,replay,options);
    player.refreshImages(false);wait(player,rep::ui::Phase::Ended);auto disabled=appliedAudio(player);
    rep::Assets disabledAssets(player.imageIndex());options.fileName=L"disabled";exportAudio(options,disabled);
    rep::exportReplay(gpu,disabledAssets,client,root,replay,options);
    player.refreshImages(true);wait(player,rep::ui::Phase::Ended);auto refreshed=appliedAudio(player);
    std::cout<<"{\"supported\":true,\"enabled\":"<<enabled<<",\"same_snapshot\":"<<(same?"true":"false")
        <<",\"disabled\":"<<sample(disabled)<<",\"refreshed\":"<<sample(refreshed)
        <<",\"new_snapshot\":"<<(refreshed!=audio&&disabled!=audio?"true":"false")<<",\"export_audio\":"<<(exported.audio?"true":"false")
        <<",\"clean_diagnostics\":"<<(clean&&exported.missingSoundCount==0?"true":"false")<<"}";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
