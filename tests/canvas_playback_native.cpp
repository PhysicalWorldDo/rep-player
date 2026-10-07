#include "engine.hpp"
#include <fstream>
#include <iostream>

static bool equalCameras(const auto& first,const auto& second) {
    if(first.size()!=second.size())return false;
    auto a=first.begin(),b=second.begin();
    for(;a!=first.end();++a,++b){const auto& x=a->second;const auto& y=b->second;
        if(a->first!=b->first||x.x!=y.x||x.y!=y.y||x.width!=y.width||x.height!=y.height||x.zoom!=y.zoom)return false;}
    return true;
}
int wmain(int argc,wchar_t** argv) {try {
    if(argc!=5)throw rep::Error("usage: canvas_playback_native ROOT REP ASSETS MODE");
    std::filesystem::path root=argv[1];std::wstring_view mode=argv[4];
    rep::ReplayOptions options;if(mode==L"context")options.profile=rep::ProtocolProfile::DnfJuly2026;
    rep::Gpu gpu(root/L"assets"/L"shaders");rep::Assets assets(argv[3]);
    rep::Replay replay(argv[2],options),referenceReplay(argv[2],options);
    rep::Executor executor(gpu,assets,root/L"cache");executor.setTransparent(true);executor.attach(replay);
    rep::Playback playback;playback.attach(replay,executor);playback.start();
    playback.step(1);playback.step(1);
    if(mode==L"hidden"){executor.setHiddenImages({"sprite/test/frame.img"});playback.refresh();}
    else if(mode==L"ended"){playback.resume();playback.select(10000);}
    else if(mode==L"stopped")playback.stop();
    else if(mode==L"running")playback.resume();
    const auto ordinal=playback.ordinal();const auto timestamp=playback.timestamp();
    const auto elapsed=playback.elapsedMilliseconds();const bool paused=playback.paused(),ended=playback.ended(),playing=playback.playing();
    const auto cameras=executor.cameras();const auto hidden=executor.hiddenImages();
    rep::CanvasSettings canvas;canvas.mode=rep::CanvasMode::Padding;canvas.left=4;canvas.top=3;canvas.right=5;canvas.bottom=6;
    playback.setCanvasSettings(canvas);
    const auto afterElapsed=playback.elapsedMilliseconds();
    const bool timePreserved=playing?afterElapsed>=elapsed&&afterElapsed-elapsed<250:afterElapsed==elapsed;
    const bool statePreserved=ordinal==playback.ordinal()&&timestamp==playback.timestamp()&&timePreserved&&paused==playback.paused()&&ended==playback.ended()&&playing==playback.playing();
    const bool cameraPreserved=equalCameras(cameras,executor.cameras());
    auto pixels=gpu.readback(executor.output());
    rep::Executor reference(gpu,assets,root/L"reference_cache");reference.setTransparent(true);reference.setCanvasSettings(canvas);reference.attach(referenceReplay);reference.setHiddenImages(hidden);
    rep::Scene scene;while(referenceReplay.next(scene)){reference.execute(scene);if(scene.ordinal>=ordinal)break;}
    auto expected=gpu.readback(reference.output());
    auto save=[&](const wchar_t* name,const rep::Bytes& bytes){std::ofstream file(root/name,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());};
    save(L"playback_actual.rgba",pixels);save(L"playback_reference.rgba",expected);
    std::cout<<"{\"width\":"<<executor.output().image.width<<",\"height\":"<<executor.output().image.height
             <<",\"ordinal\":"<<playback.ordinal()<<",\"timestamp\":"<<playback.timestamp()<<",\"state_preserved\":"<<(statePreserved?"true":"false")
             <<",\"camera_preserved\":"<<(cameraPreserved?"true":"false")<<",\"hidden_preserved\":"<<(hidden==executor.hiddenImages()?"true":"false")
             <<",\"reference_pixels_equal\":"<<(pixels==expected?"true":"false")<<",\"actual_crc\":"<<rep::crc(pixels)<<",\"reference_crc\":"<<rep::crc(expected)<<"}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
