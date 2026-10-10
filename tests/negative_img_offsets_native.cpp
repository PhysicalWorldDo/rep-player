#include "engine.hpp"
#include <fstream>
#include <iostream>

template<class T> constexpr bool hasOffsetSwitch = requires(T& value) {
    value.setNegativeImgOffsetsEnabled(true);
    value.negativeImgOffsetsEnabled();
};
template<class T> constexpr bool hasPlaybackSwitch = requires(T& value) {value.setNegativeImgOffsetsEnabled(true);};
template<class T> bool setOffsetSwitch(T& value,bool enabled) {
    if constexpr(hasPlaybackSwitch<T>){value.setNegativeImgOffsetsEnabled(enabled);return true;}
    return false;
}
static bool equalCameras(const auto& first,const auto& second) {
    if(first.size()!=second.size())return false;
    auto a=first.begin(),b=second.begin();
    for(;a!=first.end();++a,++b){const auto& x=a->second;const auto& y=b->second;
        if(a->first!=b->first||x.x!=y.x||x.y!=y.y||x.width!=y.width||x.height!=y.height||x.zoom!=y.zoom)return false;}
    return true;
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=5)throw rep::Error("usage: negative_img_offsets_native ROOT REP ASSETS MODE");
    std::filesystem::path root=argv[1],path=argv[2];std::wstring_view mode=argv[4];
    rep::ReplayOptions options;options.profile=rep::ProtocolProfile::DnfJuly2026;
    rep::Gpu gpu(root/L"assets"/L"shaders");rep::Assets assets(argv[3]);
    rep::Replay replay(path,options);rep::Executor executor(gpu,assets,root/L"cache"/path.stem());
    executor.setTransparent(true);executor.attach(replay);
    auto sourceImage=assets.preload("sprite/test/frame.img");
    auto source=sourceImage?assets.frame("sprite/test/frame.img",0,0,false):std::shared_ptr<rep::Frame>{};
    const auto originalX=source?source->x:0,originalY=source?source->y:0;
    const auto originalPixels=source?rep::crc(source->texture->rgba):0;
    bool statePreserved=true,cameraPreserved=true,hiddenPreserved=true,referenceEqual=true;
    bool pixelsChanged=true,roundTripEqual=true;
    bool switchSupported=hasOffsetSwitch<rep::Executor>,playbackSupported=hasPlaybackSwitch<rep::Playback>;
    if(mode.starts_with(L"playback-")){
        rep::Playback playback;playback.attach(replay,executor);playback.start();playback.select(0);
        playback.select(mode==L"playback-skipped"?150:50);playback.pause();
        executor.setHiddenImages({"sprite/unrelated.img"});
        if(mode==L"playback-ended"){playback.resume();playback.select(10000);}
        else if(mode==L"playback-stopped")playback.stop();
        else if(mode==L"playback-running")playback.resume();
        const auto ordinal=playback.ordinal(),selected=playback.selected,skipped=playback.skipped;
        const auto timestamp=playback.timestamp();const auto elapsed=playback.elapsedMilliseconds();
        const bool paused=playback.paused(),ended=playback.ended(),playing=playback.playing();
        const auto cameras=executor.cameras();const auto hidden=executor.hiddenImages();
        const auto before=gpu.readback(executor.output());
        setOffsetSwitch(playback,true);
        const auto afterElapsed=playback.elapsedMilliseconds();
        statePreserved=ordinal==playback.ordinal()&&timestamp==playback.timestamp()&&selected==playback.selected&&skipped==playback.skipped
            &&paused==playback.paused()&&ended==playback.ended()&&playing==playback.playing()
            &&(playing?(afterElapsed>=elapsed&&afterElapsed-elapsed<250):afterElapsed==elapsed);
        cameraPreserved=equalCameras(cameras,executor.cameras());hiddenPreserved=hidden==executor.hiddenImages();
        rep::Replay referenceReplay(path,options);rep::Executor reference(gpu,assets,root/L"reference_cache"/path.stem());
        reference.setTransparent(true);setOffsetSwitch(reference,true);reference.attach(referenceReplay);reference.setHiddenImages(hidden);
        rep::Scene scene;while(referenceReplay.next(scene)){reference.execute(scene);if(scene.ordinal>=ordinal)break;}
        const auto enabled=gpu.readback(executor.output());
        referenceEqual=enabled==gpu.readback(reference.output());pixelsChanged=enabled!=before;
        setOffsetSwitch(playback,false);roundTripEqual=gpu.readback(executor.output())==before;
        setOffsetSwitch(playback,true);
    }else{
        if(mode==L"enabled")setOffsetSwitch(executor,true);
        rep::Scene scene;while(replay.next(scene))executor.execute(scene);
    }
    auto pixels=gpu.readback(executor.output());std::ofstream file(path.replace_extension(L".rgba"),std::ios::binary);
    file.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
    const auto drawn=std::count_if(executor.currentImages().begin(),executor.currentImages().end(),[](const auto& call){return call.drawn;});
    std::cout<<"{\"supported\":"<<(switchSupported?"true":"false")<<",\"playback_supported\":"<<(playbackSupported?"true":"false")
        <<",\"width\":"<<executor.output().image.width<<",\"height\":"<<executor.output().image.height
        <<",\"state_preserved\":"<<(statePreserved?"true":"false")<<",\"camera_preserved\":"<<(cameraPreserved?"true":"false")
        <<",\"hidden_preserved\":"<<(hiddenPreserved?"true":"false")<<",\"reference_pixels_equal\":"<<(referenceEqual?"true":"false")
        <<",\"pixels_changed\":"<<(pixelsChanged?"true":"false")<<",\"round_trip_equal\":"<<(roundTripEqual?"true":"false")
        <<",\"source_preserved\":"<<(!source||(source->x==originalX&&source->y==originalY&&rep::crc(source->texture->rgba)==originalPixels)?"true":"false")
        <<",\"fallbacks\":"<<assets.fallbacks<<",\"recorded_images\":"<<executor.currentImages().size()<<",\"drawn_images\":"<<drawn
        <<",\"references\":"<<executor.statistics.references<<"}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
