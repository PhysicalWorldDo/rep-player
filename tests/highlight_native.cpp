#include "engine.hpp"
#include <fstream>
#include <iostream>
template<class E,class G> int verify(E& executor,G& gpu,rep::Replay& replay,const std::filesystem::path& out){
    if constexpr(requires{executor.enableImageHighlightTracking(true);executor.highlightFrame();}){
        executor.enableImageHighlightTracking(true);executor.setTransparent(true);executor.attach(replay);rep::Scene scene;replay.next(scene);executor.execute(scene);
        auto frame=executor.highlightFrame();auto original=gpu.readback(executor.output());
        auto mask=gpu.buildHighlightMask(*frame,"sprite/test/frame.img");
        if(!mask){std::cerr<<"FAIL selected IMG has no mask\n";return 1;}
        auto coverage=gpu.readback(*mask);auto preview=gpu.target(32,32,false);gpu.capture(executor.output(),preview);gpu.begin(preview);
        gpu.drawHighlight(*mask,{0,0,32,32},32,32);auto shown=gpu.readback(preview);
        auto at=[](const auto& bytes,int x,int y,int channel){return bytes[(y*32+x)*4+channel];};
        auto check=[&](bool value,const char* label){std::cout<<(value?"PASS ":"FAIL ")<<label<<'\n';return value;};bool ok=true;
        ok&=check(at(coverage,8,8,3)>240,"opaque black source is included");
        ok&=check(at(coverage,11,11,3)==0,"transparent RGB padding remains a hole");
        ok&=check(at(coverage,22,8,3)>240,"all current instances of selected IMG are included");
        ok&=check(at(shown,6,8,0)>240&&at(shown,6,8,1)>180&&at(shown,6,8,2)<100,"yellow outer contour");
        ok&=check(at(shown,8,8,0)==0,"source pixels keep their original color");
        ok&=check(gpu.readback(executor.output())==original,"preview overlay cannot change output or exports");
        ok&=check(gpu.buildHighlightMask(*frame,"")==nullptr,"clear selection removes the overlay");
        ok&=check(gpu.buildHighlightMask(*frame,"sprite/test/missing.img")==nullptr,"absent IMG does not create a guessed region");
        executor.setHiddenImages({"sprite/test/frame.img"});executor.redraw(scene);
        ok&=check(gpu.buildHighlightMask(*executor.highlightFrame(),"sprite/test/frame.img")==nullptr,"hidden IMG remains unhighlighted");
        // The frozen display list belongs to its own rendered frame.
        ok&=check(gpu.buildHighlightMask(*frame,"sprite/test/frame.img")!=nullptr,"frozen picture retains its original geometry");
        executor.setHiddenImages({});int transformed=0;
        while(replay.next(scene)){
            executor.execute(scene);auto pixels=gpu.readback(executor.output());auto selected=gpu.buildHighlightMask(*executor.highlightFrame(),"sprite/test/frame.img");
            bool same=selected!=nullptr;auto alpha=selected?gpu.readback(*selected):rep::Bytes{};
            if(transformed<4){for(size_t n=3;same&&n<pixels.size();n+=4){bool covered=transformed<3?pixels[n]>5:std::max({pixels[n-3],pixels[n-2],pixels[n-1]})>5;same=covered==(alpha[n]>0);}}
            else same=selected&&at(alpha,10,8,3)==0&&at(alpha,11,8,3)==0&&at(alpha,9,8,3)>240;
            ok&=check(same,transformed<3?"transformed/cropped/clipped/grid source contour matches actual draw alpha":transformed==3?"additive black padding is excluded while colored pixels retain their contour":"multiply blend excludes neutral white/transparent and half-gray pixels");transformed++;
        }
        ok&=check(transformed==5,"all transform and native blend fixtures were rendered");
        std::ofstream(out/L"preview.rgba",std::ios::binary).write(reinterpret_cast<const char*>(shown.data()),shown.size());
        std::ofstream(out/L"mask.rgba",std::ios::binary).write(reinterpret_cast<const char*>(coverage.data()),coverage.size());
        return ok?0:1;
    }else{std::cerr<<"FAIL current-frame IMG highlight support is missing\n";return 1;}
}
int wmain(int argc,wchar_t** argv){try{if(argc!=5)return 2;rep::Gpu gpu(argv[1]);rep::Assets assets(argv[2]);rep::Replay replay(argv[3]);rep::Executor executor(gpu,assets,std::filesystem::path(argv[4])/L"cache");return verify(executor,gpu,replay,argv[4]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
