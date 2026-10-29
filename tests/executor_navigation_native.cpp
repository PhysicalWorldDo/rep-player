#include "engine.hpp"
#include <iostream>

using namespace rep;
uint64_t measuredLiveRecipes=0;
uint32_t measuredRandomState=1;
int failures=0;
void expect(bool value,const char* message){if(!value){std::cout<<"FAIL "<<message<<'\n';failures++;}}
template<class E> concept SelectedExecutor=requires(E& e,const Scene& scene){
    e.executeSelected(scene,std::function<bool()>{},std::function<bool()>{});
    e.redrawSelected(scene,std::function<bool()>{},std::function<bool()>{});
};
std::vector<Scene> scenes(Replay& replay){std::vector<Scene> out;Scene s;while(replay.next(s))out.push_back(s);return out;}
std::array<uint8_t,4> pixel(const Bytes& pixels,int width,int x,int y){size_t p=size_t(y*width+x)*4;return {pixels[p],pixels[p+1],pixels[p+2],pixels[p+3]};}
template<class E> void contract(E& e,Gpu& gpu,const std::vector<Scene>& records){
    if constexpr(!SelectedExecutor<E>)expect(false,"transactional selected-scene execution is absent");
    else {
        expect(e.executeSelected(records[0]),"first selected scene commits");
        auto before=gpu.readback(e.output());auto beforeCameras=e.cameras();auto beforeScenes=e.statistics.scenes;
        expect(!e.executeSelected(records[1],{},[]{return false;}),"rejected presentation gate rolls back");
        expect(gpu.readback(e.output())==before,"rejected target preserves displayed pixels");
        expect(e.statistics.scenes==beforeScenes,"rejected target does not publish execution counters");
        expect(e.cameras().size()==beforeCameras.size(),"rejected target preserves camera registry");
        expect(e.executeSelected(records[2]),"read-only cache consumer commits");
        auto left=gpu.readback(e.output());
        expect(pixel(left,32,2,2)[0]>240&&pixel(left,32,14,2)[0]==0,"cancelled context overwrite does not affect next scene");
        expect(e.executeSelected(records[1]),"context read followed by overwrite commits");
        auto shown=gpu.readback(e.output());
        expect(e.redrawSelected(records[1]),"current scene refresh commits");
        expect(gpu.readback(e.output())==shown,"refresh reuses entry context pixels before same-scene overwrite");
        e.setHiddenImages({"sprite/mask/frame.img"});
        expect(e.redrawSelected(records[1]),"filter refresh commits");
        auto hidden=gpu.readback(e.output());
        expect(pixel(hidden,32,14,2)[0]==0&&pixel(hidden,32,2,2)[0]==0,"filter regenerates baked context inputs from recipes");
        e.setHiddenImages({});expect(e.redrawSelected(records[1]),"restored filter refresh commits");
        expect(gpu.readback(e.output())==shown,"restoring visibility regenerates previous context contents");
        CanvasSettings canvas;canvas.mode=CanvasMode::Padding;canvas.left=8;canvas.top=5;canvas.right=9;canvas.bottom=6;
        e.setCanvasSettings(canvas);expect(e.redrawSelected(records[1]),"canvas refresh commits");
        auto expanded=gpu.readback(e.output());
        expect(e.output().image.width==49&&e.output().image.height==43,"canvas uses requested dimensions");
        expect(pixel(expanded,49,10,7)[0]>240&&pixel(expanded,49,22,7)[0]==0,"canvas rebuild preserves entry-mask world placement");
        expect(pixel(expanded,49,41,7)[0]>240,"canvas rebuild regenerates pixels beyond the former canvas edge");
        auto stable=expanded;int checks=0;
        expect(!e.executeSelected(records[0],[&]{return ++checks>2;}),"instruction cancellation is accepted");
        expect(gpu.readback(e.output())==stable,"instruction cancellation preserves current output");
        bool threw=false;try{e.executeSelected(records[3]);}catch(const Error&){threw=true;}
        expect(threw&&gpu.readback(e.output())==stable,"invalid target rolls back its completed context writes");
        for(int n=0;n<256;n++)expect(e.executeSelected(records[1]),"repeated context production commits");
        expect(measuredLiveRecipes<=4,"independent context overwrites release unrelated scene recipes");
    }
}
template<class E> void negativeContract(E& e,Gpu& gpu,const std::vector<Scene>& records){
    if constexpr(SelectedExecutor<E>){
        e.setNegativeImgOffsetsEnabled(true);e.executeSelected(records[0]);e.executeSelected(records[1]);auto shown=gpu.readback(e.output());
        expect(pixel(shown,32,2,2)[0]>240,"negative-offset mask is visible when enabled");
        e.setNegativeImgOffsetsEnabled(false);e.redrawSelected(records[1]);
        expect(pixel(gpu.readback(e.output()),32,2,2)[0]==0,"negative-offset edit regenerates previously baked mask");
        e.setNegativeImgOffsetsEnabled(true);e.redrawSelected(records[1]);
        expect(gpu.readback(e.output())==shown,"restoring negative offsets regenerates its previous pixels");
    }
}
template<class E> void randomContract(E& e,Gpu& gpu,const std::vector<Scene>& records){
    if constexpr(SelectedExecutor<E>){
        e.executeSelected(records[0]);uint32_t candidate=0;
        expect(!e.executeSelected(records[0],{},[&]{candidate=measuredRandomState;return false;}),"random candidate can be rejected");
        e.executeSelected(records[0]);expect(measuredRandomState==candidate,"cancelled shader does not advance committed random state");
        auto pixels=gpu.readback(e.output());e.redrawSelected(records[0]);
        expect(measuredRandomState==candidate&&gpu.readback(e.output())==pixels,"current-scene refresh restores its random entry state");
    }
}
template<class E> void dependencyContract(E& e,Gpu& gpu,const std::vector<Scene>& records){
    if constexpr(SelectedExecutor<E>){
        for(const auto& scene:records)e.executeSelected(scene);auto shown=gpu.readback(e.output());
        expect(pixel(shown,32,3,3)[0]>240,"dependent context contains its producer's cached mask");
        e.setHiddenImages({"sprite/mask/frame.img"});int checks=0;
        expect(!e.redrawSelected(records[2],[&]{return ++checks>4;}),"recursive context regeneration can be cancelled");
        expect(gpu.readback(e.output())==shown,"cancelled dependency regeneration preserves displayed pixels");
        e.redrawSelected(records[2]);expect(pixel(gpu.readback(e.output()),32,3,3)[0]==0,"visibility changes propagate through active context dependencies");
        e.setHiddenImages({});e.redrawSelected(records[2]);expect(gpu.readback(e.output())==shown,"dependency recipes restore after visibility is reenabled");
    }
}
template<class E> void movieContract(E& e,Gpu& gpu,const std::vector<Scene>& records){
    if constexpr(SelectedExecutor<E>){
        e.executeSelected(records[0]);auto shown=gpu.readback(e.output());
        expect(!e.executeSelected(records[1],{},[]{return false;}),"decoded movie target can be rejected");
        expect(gpu.readback(e.output())==shown,"rejected movie keeps displayed pixels");
        expect(gpu.textureCacheBytes()==0,"rejected movie releases GPU source before its logical binding is rolled back");
    }
}
template<class E> void captureContract(E& e,Gpu& gpu,const std::vector<Scene>& records){
    if constexpr(!SelectedExecutor<E>)expect(false,"transactional selected-scene execution is absent");
    else {
        e.executeSelected(records[0]);e.executeSelected(records[1]);auto shown=gpu.readback(e.output());
        expect(pixel(shown,32,3,3)[0]>240,"combined current local mask and cached context are visible");
        e.setHiddenImages({"sprite/unused/frame.img"});e.redrawSelected(records[1]);
        expect(gpu.readback(e.output())==shown,"context regeneration preserves current-scene capture scratch pixels and flags");
    }
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=6)return 2;std::wstring mode=argv[1];ReplayOptions options;if(mode!=L"sampler"&&mode!=L"random")options.profile=ProtocolProfile::DnfJuly2026;
    Replay replay(argv[4],options);Gpu gpu(argv[2]);Assets assets(argv[3]);Executor executor(gpu,assets,argv[5]);executor.attach(replay);executor.setTransparent(true);auto records=scenes(replay);
    if(mode==L"sampler"){
        for(const auto& scene:records)executor.execute(scene);auto image=gpu.readback(executor.output());
        expect(pixel(image,16,4,1)[0]>240&&pixel(image,16,1,1)[0]==0,"native sampler offset survives an ineligible draw");
    }else if(mode==L"capture")captureContract(executor,gpu,records);
    else if(mode==L"negative")negativeContract(executor,gpu,records);
    else if(mode==L"random")randomContract(executor,gpu,records);
    else if(mode==L"dependencies")dependencyContract(executor,gpu,records);
    else if(mode==L"movie")movieContract(executor,gpu,records);
    else contract(executor,gpu,records);
    std::cout<<"failures "<<failures<<'\n';return failures?1:0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}}
