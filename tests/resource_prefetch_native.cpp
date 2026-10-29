#include "resources.hpp"
#include "gpu.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace rep {
static std::atomic<unsigned> cpuAccounting=0,gpuAccounting=0;
static std::mutex barrierMutex;
static std::condition_variable barrierCondition;
static std::string blockedPath;
static bool entered=false,released=false;
static std::vector<std::string> decodeOrder;
void resourceTestCpuAccounting(){cpuAccounting++;}
void resourceTestGpuAccounting(){gpuAccounting++;}
void resourceTestDecodeBarrier(const std::string& name){
    if((name.starts_with("publish:")||name.starts_with("framepub:"))&&name!=blockedPath)return;
    std::unique_lock lock(barrierMutex);decodeOrder.push_back(name);
    if(name!=blockedPath||entered)return;
    entered=true;barrierCondition.notify_all();barrierCondition.wait(lock,[]{return released;});
}
}
static void expect(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static void block(std::string path){std::lock_guard lock(rep::barrierMutex);rep::blockedPath=std::move(path);rep::entered=false;rep::released=false;rep::decodeOrder.clear();}
static void waitEntered(){std::unique_lock lock(rep::barrierMutex);expect(rep::barrierCondition.wait_for(lock,std::chrono::seconds(5),[]{return rep::entered;}),"background decode did not reach its barrier");}
static void release(){std::lock_guard lock(rep::barrierMutex);rep::released=true;rep::barrierCondition.notify_all();}
template<class T> static constexpr bool hasPrefetch=requires(T& assets,const std::vector<typename T::FrameRequest>& requests){assets.prefetch(requests,uint64_t(1));assets.drainReady(uint64_t(1),size_t(32));assets.frameReady(std::string{},0);assets.prefetchStats();};
template<class T> static void exercise(T& assets,const std::filesystem::path& root,std::wstring_view mode){
    if constexpr(!hasPrefetch<T>)throw std::runtime_error("Assets asynchronous CPU prefetch API is missing");
    else {
        using Request=typename T::FrameRequest;auto owner=std::this_thread::get_id();std::atomic<unsigned> observed=0,waited=0;std::atomic<bool> wrongThread=false;
        assets.setFrameObserver([&](const std::string&,int){observed++;if(std::this_thread::get_id()!=owner)wrongThread=true;});
        assets.setDemandWaitCallback([&]{waited++;if(std::this_thread::get_id()!=owner)wrongThread=true;});
        auto ready=[&](uint64_t generation,size_t wanted){std::vector<std::shared_ptr<rep::Frame>> frames;auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(frames.size()<wanted&&std::chrono::steady_clock::now()<until){auto next=assets.drainReady(generation,64);frames.insert(frames.end(),next.begin(),next.end());std::this_thread::sleep_for(std::chrono::milliseconds(1));}expect(frames.size()==wanted,"prefetch failed to publish the expected ready frames");return frames;};
        if(mode==L"active-working-set"){
            auto first=assets.frame("sprite/large0/frame.img",0);auto pinned=first->texture;first.reset();
            std::vector<Request> requests;for(int n=0;n<33;n++)requests.push_back({"sprite/large"+std::to_string(n)+"/frame.img",0,1});assets.prefetch(requests,1);
            auto until=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(assets.prefetchStats().completed<33&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(2));
            expect(assets.prefetchStats().completed==33,"large active prefetch did not complete");
            expect(assets.frame("sprite/large0/frame.img",0)->texture==pinned,"active prefetch evicted the current replay's decoded pixels and decoded a second GPU source");
            expect(assets.decodedImageBytes()>128*1024*1024,"active replay working set was silently capped by the retention budget");
            assets.pruneDecodedImages(128*1024*1024);expect(assets.decodedImageBytes()<=128*1024*1024,"explicit cross-REP retention prune exceeds 128MiB");
            assets.pruneDecodedImages(0);expect(assets.decodedImageBytes()==0&&pinned->rgba.size()==1024*1024*4&&pinned->rgba[0]==30,"explicit retention pruning invalidated externally pinned pixels");
        }else if(mode==L"ready"){
            assets.prefetch(std::vector<Request>{{"sprite/cache0/frame.img",0,1}},1);auto frames=ready(1,1);
            expect(observed==0&&waited==0,"background worker invoked render-owned callbacks");expect(assets.frameReady("sprite/cache0/frame.img",0),"completed frame is not synchronously ready");
            auto frame=assets.frame("sprite/cache0/frame.img",0);expect(frame->texture==frames[0]->texture,"demand decoded a second copy of ready pixels");expect(observed==1&&waited==0&&!wrongThread,"ready hit callback ownership changed");expect(assets.prefetchStats().completed==1,"completed job statistics missing");
        }else if(mode==L"priority"){
            block("sprite/cache0/frame.img");assets.prefetch(std::vector<Request>{{"sprite/cache0/frame.img",0,0},{"sprite/cache1/frame.img",0,2},{"sprite/cache2/frame.img",0,1}},1);waitEntered();
            assets.prefetch(std::vector<Request>{{"sprite/cache3/frame.img",0,0}},1);release();ready(1,4);
            std::lock_guard lock(rep::barrierMutex);expect(rep::decodeOrder.size()==4,"duplicate queue decode occurred");expect(rep::decodeOrder[1]=="sprite/cache3/frame.img"&&rep::decodeOrder[2]=="sprite/cache2/frame.img","current/near requests did not overtake queued dictionary work");
        }else if(mode==L"demand-wait"){
            block("sprite/cache0/frame.img");assets.prefetch(std::vector<Request>{{"sprite/cache0/frame.img",0,2}},1);waitEntered();
            auto unblock=std::async(std::launch::async,[]{std::this_thread::sleep_for(std::chrono::milliseconds(30));release();});auto frame=assets.frame("sprite/cache0/frame.img",0);unblock.get();
            expect(frame->texture->rgba[0]==30,"waiting demand pixels changed");expect(waited==1&&observed==1&&!wrongThread,"demand wait did not notify the render owner exactly once");auto stats=assets.prefetchStats();expect(stats.demandWaits==1&&stats.demandWaitMilliseconds>0,"demand wait metrics missing");
            std::lock_guard lock(rep::barrierMutex);expect(rep::decodeOrder.size()==1,"demand raced the worker and decoded the same frame twice");
        }else if(mode==L"cancel"){
            block("sprite/cache0/frame.img");assets.prefetch(std::vector<Request>{{"sprite/cache0/frame.img",0,0}},1);waitEntered();assets.prefetch(std::vector<Request>{{"sprite/cache1/frame.img",0,0}},2);release();ready(2,1);expect(assets.drainReady(1,32).empty(),"obsolete generation published ready output");expect(assets.prefetchStats().cancelled>=1,"cancelled work was not recorded");
        }else if(mode==L"cancel-demand"){
            block("sprite/cache0/frame.img");assets.prefetch(std::vector<Request>{{"sprite/cache0/frame.img",0,0}},1);waitEntered();std::atomic<bool> cancelled=false;assets.setDemandCancellation([&]{return cancelled.load();});
            auto unblock=std::async(std::launch::async,[&]{std::this_thread::sleep_for(std::chrono::milliseconds(15));cancelled=true;std::this_thread::sleep_for(std::chrono::milliseconds(60));release();});
            auto before=std::chrono::steady_clock::now();bool failed=false;try{assets.frame("sprite/cache0/frame.img",0);}catch(const rep::Error&){failed=true;}auto elapsed=std::chrono::steady_clock::now()-before;unblock.get();assets.setDemandCancellation({});expect(failed&&elapsed<std::chrono::milliseconds(60),"demand cancellation waited for the background decode");expect(assets.frame("sprite/cache0/frame.img",0)->texture->rgba[0]==30,"cancelled demand poisoned later requests");
        }else if(mode==L"hot"){
            assets.frame("sprite/cache0/frame.img",0);assets.setFrameObserver({});assets.setDemandWaitCallback({});block("sprite/cache1/frame.img");assets.prefetch(std::vector<Request>{{"sprite/cache1/frame.img",0,2}},1);waitEntered();
            auto hot=std::async(std::launch::async,[&]{return assets.frame("sprite/cache0/frame.img",0)->texture->rgba[0];});bool immediate=hot.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;release();expect(hot.get()==30,"hot frame changed");expect(immediate,"cold background decode blocked an unrelated hot frame");
        }else if(mode==L"revision-gate"){
            if constexpr(requires(T& value){value.publishAtRevision(uint64_t(0),std::function<void()>{});}){
                T rejecter(assets.index());auto revision=assets.sourceRevision();unsigned installations=0;
                expect(!assets.publishAtRevision(revision+1,[&]{installations++;})&&installations==0,"stale revision ran the transaction installation");
                block("revision-gate");auto installing=std::async(std::launch::async,[&]{return assets.publishAtRevision(revision,[&]{installations++;rep::resourceTestDecodeBarrier("revision-gate");});});waitEntered();
                auto rejection=std::async(std::launch::async,[&]{try{rejecter.frame("sprite/valid/frame.img",99,0,false);}catch(const rep::Error&){}return rejecter.sourceRevision();});
                bool atomic=rejection.wait_for(std::chrono::milliseconds(30))!=std::future_status::ready;release();expect(installing.get()&&installations==1,"matching revision did not install exactly once");expect(rejection.get()==1&&atomic,"source rejection interleaved the transaction installation");
                expect(!assets.publishAtRevision(revision,[&]{installations++;})&&installations==1,"old revision installed after shared rejection");
            }else throw std::runtime_error("Assets publishAtRevision gate API is missing");
        }else if(mode==L"publish-atomic"||mode==L"framepub-atomic"){
            T rejecter(assets.index());bool frame=mode==L"framepub-atomic";block(std::string(frame?"framepub:":"publish:")+"sprite/valid/frame.img");
            auto publishing=std::async(std::launch::async,[&]{if(frame)assets.frame("sprite/valid/frame.img",0);else assets.preload("sprite/valid/frame.img");});waitEntered();
            auto rejection=std::async(std::launch::async,[&]{try{rejecter.frame("sprite/valid/frame.img",99,0,false);}catch(const rep::Error&){}return rejecter.sourceRevision();});
            bool atomic=rejection.wait_for(std::chrono::milliseconds(30))!=std::future_status::ready;release();publishing.get();expect(rejection.get()==1,"rejection did not advance the shared revision");expect(atomic,"shared rejection crossed the final revision check and cache installation");
        }else if(mode==L"revision"){
            T rejecter(assets.index());block("sprite/valid/frame.img");assets.prefetch(std::vector<Request>{{"sprite/valid/frame.img",0,0}},1);waitEntered();bool failed=false;try{rejecter.frame("sprite/valid/frame.img",99,0,false);}catch(const rep::Error&){failed=true;}expect(failed&&assets.sourceRevision()==1,"test did not reject the whole patch path");release();
            auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(assets.prefetchStats().completed+assets.prefetchStats().cancelled+assets.prefetchStats().failed==0&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
            for(auto& frame:assets.drainReady(1,32))expect(frame->texture->rgba[0]==10,"obsolete patch completion was published");expect(assets.frame("sprite/valid/frame.img",0)->texture->rgba[0]==10,"demand ignored the shared whole-path rejection");
        }else if(mode==L"future-error"){
            assets.prefetch(std::vector<Request>{{"sprite/broken/frame.img",0,2},{"sprite/cache0/frame.img",0,1}},1);ready(1,1);auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!assets.prefetchStats().failed&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));expect(assets.prefetchStats().failed==1,"future error not retained by its job");expect(assets.frame("sprite/cache0/frame.img",0)->texture->rgba[0]==30,"future error broke an unrelated target");bool failed=false;try{assets.frame("sprite/broken/frame.img",0);}catch(const rep::Error&){failed=true;}expect(failed,"demand swallowed the resource error discovered during prefetch");
        }else throw std::runtime_error("unknown prefetch mode");
    }
}
int wmain(int argc,wchar_t** argv){try{
    expect(argc==3,"mode and fixture root required");auto mode=std::wstring_view(argv[1]);std::filesystem::path root=argv[2];
    if(mode==L"prune-cpu"){
        rep::Assets assets(root,false);std::shared_ptr<rep::Frame> pinned;for(int n=0;n<32;n++){auto frame=assets.frame("sprite/cache"+std::to_string(n)+"/frame.img",0);if(n==0)pinned=frame;}rep::cpuAccounting=0;assets.pruneDecodedImages(0);expect(rep::cpuAccounting<=2,"CPU prune rescanned the complete cache for each evicted IMG");expect(assets.decodedImageBytes()==0&&pinned->texture->rgba[0]==30,"CPU prune changed byte accounting or pinned lifetime");
    }else if(mode==L"prune-gpu"){
        rep::Gpu gpu(root);std::shared_ptr<rep::GpuTexture> pinned;for(int n=0;n<32;n++){auto pixels=std::make_shared<rep::Pixels>();pixels->width=pixels->height=64;pixels->rgba.assign(64*64*4,uint8_t(n));auto texture=gpu.texture(pixels);if(n==0)pinned=texture;}rep::gpuAccounting=0;gpu.pruneTextures(0);expect(rep::gpuAccounting<=1,"GPU prune rescanned the complete cache for each evicted texture");expect(gpu.textureCacheBytes()==0&&pinned->width==64,"GPU prune changed budget or pinned lifetime");
    }else {rep::Assets assets(root,mode==L"revision"||mode==L"publish-atomic"||mode==L"framepub-atomic"||mode==L"revision-gate");exercise(assets,root,mode);}
    std::cout<<"{\"mode\":\""<<rep::utf8(argv[1])<<"\",\"passed\":true}\n";return 0;
}catch(const std::exception& error){release();std::cerr<<error.what()<<'\n';return 1;}}
