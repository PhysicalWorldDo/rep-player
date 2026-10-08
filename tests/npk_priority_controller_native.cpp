#include "ui_playback.hpp"
#include <fstream>
#include <iostream>
#include <thread>

using namespace rep::ui;

template<class T> bool refreshImages(T& player) {
    if constexpr(requires { player.refreshImages(); }) { player.refreshImages();return true; }
    else { player.replay();return false; }
}
template<class T> std::shared_ptr<const void> imageIndex(T& player) {
    if constexpr(requires { player.imageIndex(); })return player.imageIndex();
    else return {};
}
template<class Predicate> PlayerStatus waitFor(PlaybackController& player,Predicate predicate) {
    const auto deadline=Clock::now()+std::chrono::seconds(15);
    while(Clock::now()<deadline) {
        MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        auto status=player.status();
        if(status.phase==Phase::Error)throw rep::Error(rep::utf8(status.message));
        if(predicate(status))return status;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw rep::Error("NPK controller probe timed out");
}
static uint32_t capture(PlaybackController& player) {
    auto serial=player.status().captureSerial;player.captureFrame();
    return waitFor(player,[&](const auto& status){return status.captureSerial>serial;}).frameCrc;
}
static uint32_t reference(HWND window,const std::filesystem::path& root,const std::filesystem::path& client,const std::filesystem::path& replay,rep::CanvasSettings canvas={}) {
    PlaybackController player(window,root,client,false,Clock::now(),{},canvas);player.open(replay);
    waitFor(player,[](const auto& status){return status.phase==Phase::Ended;});return capture(player);
}
static void writeBoolean(std::string_view key,bool value,bool& first) {
    if(!first)std::cout<<',';first=false;std::cout<<'"'<<key<<"\":"<<(value?"true":"false");
}

int wmain(int argc,wchar_t** argv) {
    if(argc!=4)return 2;
    HWND window=nullptr;
    try {
        const std::wstring mode=argv[1];const std::filesystem::path root=argv[2],client=argv[3];
        window=CreateWindowW(L"STATIC",L"NPK priority controller regression",WS_OVERLAPPEDWINDOW,0,0,256,256,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window)throw rep::Error("Cannot create owned NPK regression window");
        std::map<std::string,bool> results;
        if(mode==L"empty") {
            PlaybackController player(window,root,client,false,Clock::now());
            bool api=refreshImages(player);results["refresh_api"]=api;
            if(api)waitFor(player,[&](const auto& status){return status.phase==Phase::Empty&&bool(imageIndex(player));});
            results["empty_snapshot_available"]=bool(imageIndex(player));results["empty_stays_empty"]=player.status().phase==Phase::Empty;
        } else if(mode==L"priority") {
            rep::CanvasSettings canvas;canvas.mode=rep::CanvasMode::Multiple;canvas.factor=2;
            const auto firstReplay=client/L"Replay"/L"first.rep",secondReplay=client/L"Replay"/L"second.rep";
            const auto expected=reference(window,root,root/L"reference_client",firstReplay,canvas);
            {
                PlaybackController player(window,root,client,false,Clock::now(),{},canvas);player.setAudioVolume(.37f);player.setAudioMuted(true);
                player.open(firstReplay);waitFor(player,[](const auto& status){return status.phase==Phase::Ended;});auto original=capture(player);auto originalIndex=imageIndex(player);
                results["fixture_colors_differ"]=expected!=original;
                std::filesystem::copy_file(root/L"patch_green.NPK",client/L"ImagePacks2"/L"000_refresh_patch.NPK");
                player.replay();waitFor(player,[](const auto& status){return status.phase==Phase::Ended;});results["ordinary_replay_keeps_snapshot"]=capture(player)==original&&imageIndex(player)==originalIndex;
                player.open(secondReplay);waitFor(player,[](const auto& status){return status.phase==Phase::Ended;});results["switch_replay_keeps_snapshot"]=capture(player)==original&&imageIndex(player)==originalIndex;
                player.setHiddenImages({"sprite/optional/hidden.img"});waitFor(player,[](const auto& status){return status.hiddenImages.contains("sprite/optional/hidden.img");});
                bool api=refreshImages(player);results["refresh_api"]=api;results["refresh_clears_published_snapshot"]=!originalIndex||imageIndex(player)!=originalIndex;
                waitFor(player,[](const auto& status){return status.phase==Phase::Ended;});results["refresh_applies_added_patch"]=capture(player)==expected;
                auto status=player.status();results["refresh_preserves_settings"]=status.hiddenImages.contains("sprite/optional/hidden.img")&&status.canvasWidth==32&&status.canvasHeight==32&&status.audioMuted&&std::abs(status.audioVolume-.37f)<.0001f;
                results["refresh_publishes_new_snapshot"]=bool(imageIndex(player))&&imageIndex(player)!=originalIndex;
                std::filesystem::copy_file(root/L"patch_red.NPK",client/L"ImagePacks2"/L"001_second_patch.NPK");
                std::filesystem::rename(client/L"ImagePacks2"/L"000_refresh_patch.NPK",client/L"ImagePacks2"/L"002_refresh_patch.NPK");
                refreshImages(player);waitFor(player,[](const auto& state){return state.phase==Phase::Ended;});results["refresh_rename_changes_priority"]=capture(player)==original;
                std::filesystem::remove(client/L"ImagePacks2"/L"001_second_patch.NPK");
                refreshImages(player);waitFor(player,[](const auto& state){return state.phase==Phase::Ended;});results["refresh_reopens_renamed_patch"]=capture(player)==expected;
                std::filesystem::remove(client/L"ImagePacks2"/L"002_refresh_patch.NPK");
                refreshImages(player);waitFor(player,[](const auto& state){return state.phase==Phase::Ended;});results["refresh_removal_restores_original"]=capture(player)==original;
            }
        } else if(mode==L"same_root") {
            const auto replay=client/L"Replay"/L"first.rep";auto expected=reference(window,root,root/L"reference_client",replay);
            PlaybackController player(window,root,client,false,Clock::now());player.open(replay);waitFor(player,[](const auto& status){return status.phase==Phase::Ended;});auto original=capture(player);auto oldIndex=imageIndex(player);
            std::filesystem::copy_file(root/L"patch_green.NPK",client/L"ImagePacks2"/L"000_refresh_patch.NPK");
            player.configureClient(client);results["configure_clears_published_snapshot"]=!oldIndex||imageIndex(player)!=oldIndex;
            waitFor(player,[](const auto& status){return status.phase==Phase::Empty;});player.open(replay);waitFor(player,[](const auto& status){return status.phase==Phase::Ended;});
            results["fixture_colors_differ"]=expected!=original;results["same_root_rebuilds_images"]=capture(player)==expected;results["same_root_new_snapshot"]=bool(imageIndex(player))&&imageIndex(player)!=oldIndex;
        } else if(mode==L"shader") {
            const auto replay=client/L"Replay"/L"shader.rep";
            PlaybackController player(window,root,client,false,Clock::now());player.open(replay);waitFor(player,[](const auto& status){return status.phase==Phase::Ended;});auto original=capture(player);
            std::filesystem::copy_file(root/L"shader_changed.NPK",client/L"ImagePacks2"/L"sprite_shader.NPK",std::filesystem::copy_options::overwrite_existing);
            auto expected=reference(window,root,client,replay);results["shader_fixture_changes_output"]=expected!=original;
            refreshImages(player);waitFor(player,[](const auto& status){return status.phase==Phase::Ended;});results["refresh_rebuilds_shader_builtin"]=capture(player)==expected;
        } else throw rep::Error("Unknown controller probe mode");
        DestroyWindow(window);window=nullptr;std::cout<<'{';bool first=true;for(const auto& [key,value]:results)writeBoolean(key,value,first);std::cout<<"}\n";return 0;
    }catch(const std::exception& error){if(window)DestroyWindow(window);std::cerr<<error.what()<<'\n';return 2;}
}
