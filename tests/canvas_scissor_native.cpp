#include "engine.hpp"
#include <iostream>

int wmain(int argc,wchar_t** argv){try{
    if(argc!=4)throw rep::Error("expected run root, replay and asset directory");
    std::filesystem::path root(argv[1]);rep::Gpu gpu(root/L"assets"/L"shaders");
    rep::Assets assets(argv[3]);rep::Replay replay(argv[2]);std::array<rep::Scene,2> scenes;
    for(auto& scene:scenes)if(!replay.next(scene))throw rep::Error("expected full and mixed clip scenes");
    rep::Executor executor(gpu,assets,root/L"runtime"/L"cache");executor.attach(replay);
    std::array<rep::CanvasSettings,6> settings;
    settings[1].mode=rep::CanvasMode::Padding;settings[1].left=4;settings[1].top=3;settings[1].right=5;settings[1].bottom=6;
    settings[3].factor=2;
    settings[4].mode=rep::CanvasMode::Size;settings[4].width=25;settings[4].height=23;
    settings[5].mode=rep::CanvasMode::Size;settings[5].width=8;settings[5].height=8;
    std::cout<<'{';
    for(size_t sample=0;sample<scenes.size();sample++){
    if(sample)std::cout<<',';
    std::cout<<(sample?"\"mixed_edge_scissor_enabled\":[":"\"full_screen_scissor_enabled\":[");
    for(size_t n=0;n<settings.size();n++){
        executor.setCanvasSettings(settings[n]);executor.executeSelected(scenes[sample]);gpu.flush();
        rep::Com<ID3D11RasterizerState> state;gpu.context()->RSGetState(state.out());
        if(!state)throw rep::Error("missing rasterizer state");D3D11_RASTERIZER_DESC description{};state->GetDesc(&description);
        if(n)std::cout<<',';std::cout<<(description.ScissorEnable?"true":"false");
    }
    std::cout<<']';
    }
    std::cout<<"}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
