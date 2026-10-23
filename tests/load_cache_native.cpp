#include "resources.hpp"
#include "gpu.hpp"
#include <iostream>
#include <stdexcept>
#include <future>
#include <latch>

namespace rep {
// The test runner injects this call only into its isolated resources.cpp copy.
// Production builds have no hook or wait in the decoder.
static std::latch* validationEntered=nullptr;
static std::latch* validationRelease=nullptr;
static std::atomic<bool> validationBlocked=false;
void loadCacheValidationBarrier(){if(validationEntered&&!validationBlocked.exchange(true)){validationEntered->count_down();validationRelease->wait();}}
}

static void expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T> static void pruneAssets(T& assets,size_t bytes){if constexpr(requires{assets.pruneDecodedImages(bytes);})assets.pruneDecodedImages(bytes);else assets.clearDecodedImages();}
template<class T> static size_t assetBytes(T& assets){if constexpr(requires{assets.decodedImageBytes();})return assets.decodedImageBytes();else return 0;}
template<class T> static uint64_t validations(T& assets){if constexpr(requires{assets.patchValidations;})return assets.patchValidations;else return 1000;}
template<class T> static void pruneGpu(T& gpu,size_t bytes){if constexpr(requires{gpu.pruneTextures(bytes);})gpu.pruneTextures(bytes);else gpu.resetTextures();}
template<class T> static size_t gpuBytes(T& gpu){if constexpr(requires{gpu.textureCacheBytes();})return gpu.textureCacheBytes();else return 0;}
static std::shared_ptr<rep::Pixels> pixels(unsigned char value){auto p=std::make_shared<rep::Pixels>();p->width=p->height=128;p->rgba.assign(128*128*4,value);return p;}
int wmain(int argc,wchar_t** argv){try{
    expect(argc==3,"mode and fixture root required");std::filesystem::path root=argv[2];auto mode=std::wstring_view(argv[1]);
    if(mode==L"assets"){
        rep::Assets assets(root,false);auto a=assets.preload("sprite/cache0/frame.img");auto af=assets.frame("sprite/cache0/frame.img",0);std::weak_ptr<rep::Img> wa=a;std::weak_ptr<rep::Pixels> wp=af->texture;a.reset();af.reset();auto one=assetBytes(assets);
        auto alias=assets.frame("cache0/frame.img",0);expect(assetBytes(assets)-one<4096,"shared pixels counted repeatedly");alias.reset();
        auto b=assets.preload("sprite/cache1/frame.img");assets.frame("sprite/cache1/frame.img",0);std::weak_ptr<rep::Img> wb=b;b.reset();
        auto c=assets.preload("sprite/cache2/frame.img");assets.frame("sprite/cache2/frame.img",0);std::weak_ptr<rep::Img> wc=c;c.reset();
        assets.frame("sprite/cache0/frame.img",0);auto budget=one*2+4096;pruneAssets(assets,budget);
        expect(!wa.expired(),"recent IMG was discarded across REP boundary");expect(wb.expired(),"least recently used IMG remains owned after eviction");expect(!wc.expired(),"newer IMG was discarded before oldest IMG");expect(assetBytes(assets)<=budget,"CPU cache exceeds explicit budget");
        auto crc=rep::crc(assets.frame("sprite/cache1/frame.img",0)->texture->rgba);expect(crc!=0,"evicted IMG cannot reload");pruneAssets(assets,0);expect(wa.expired()&&wc.expired()&&wp.expired(),"zero-budget prune retains old images or pixels");
        auto old=assets.preload("sprite/cache0/frame.img");std::weak_ptr<rep::Img> clear=old;old.reset();assets.clearDecodedImages();expect(clear.expired(),"clearDecodedImages no longer fully clears");
    }else if(mode==L"patch"){
        rep::Assets assets(root,true);auto f=assets.frame("sprite/valid/frame.img",0);expect(f->texture->rgba[0]==90,"valid patch pixels changed");expect(validations(assets)==1,"valid whole-IMG validation was not counted once");f.reset();assets.clearDecodedImages();assets.frame("sprite/valid/frame.img",0);expect(validations(assets)==1,"whole-IMG validation repeated after image eviction");
        auto index=assets.index();auto concurrent=std::async(std::launch::async,[&]{rep::Assets other(index);other.frame("sprite/valid/frame.img",1);return validations(other);});expect(concurrent.get()==0,"shared Index did not reuse successful validation");
        auto bad=assets.frame("sprite/bad/frame.img",0);expect(bad->texture->rgba[0]==20,"bad unused patch frame did not force whole-path original fallback");expect(assets.sourceRevision()>0,"bad patch did not update shared revision");assets.frame("sprite/valid/frame.img",0);expect(validations(assets)==3,"source revision did not invalidate successful validation conclusions");
        rep::Assets other(index);expect(other.frame("sprite/bad/frame.img",0)->texture->rgba[0]==20,"shared export reader ignored rejected patch");rep::Assets fresh(root,true);fresh.frame("sprite/valid/frame.img",0);expect(validations(fresh)==1,"new Index reused stale validation conclusions");
        auto freshIndex=fresh.index();std::latch begin(2);auto reject=[&]{rep::Assets reader(freshIndex);begin.arrive_and_wait();auto frame=reader.frame("sprite/bad/frame.img",0);return std::pair{frame->texture->rgba[0],reader.sourceRevision()};};
        auto first=std::async(std::launch::async,reject),second=std::async(std::launch::async,reject);auto r1=first.get(),r2=second.get();expect(r1.first==20&&r2.first==20&&r1.second==1&&r2.second==1,"concurrent preview/export rejection was inconsistent");
    }else if(mode==L"alias-palette"||mode==L"alias-hd"){
        bool palette=mode==L"alias-palette";std::string logical=palette?"sprite/weapon/katana0111b.img":"sprite/interface/handle.img";
        std::string source=palette?"sprite/weapon/katana0100b.img":"sprite/interface/(2x)handle.img";
        rep::Assets assets(root,false);auto frame=assets.frame(logical,0);expect(frame->actualPath==logical,"alias lost its logical placement metadata");expect(frame->geometryScale==(palette?1.f:.5f),"alias geometry changed");expect(frame->texture->rgba[0]==(palette?41:70),"actual palette/HD source was not resolved");frame.reset();
        auto image=assets.preload(source);std::weak_ptr<rep::Img> recent=image;image.reset();auto one=assetBytes(assets);
        auto b=assets.preload("sprite/cache1/frame.img");assets.frame("sprite/cache1/frame.img",0);std::weak_ptr<rep::Img> oldest=b;b.reset();
        assets.frame("sprite/cache2/frame.img",0);assets.frame(logical,0);pruneAssets(assets,one*2+4096);
        expect(!recent.expired(),"hot palette/HD alias source was evicted as least recently used");expect(oldest.expired(),"palette/HD alias did not refresh its real source LRU entry");
    }else if(mode==L"patch-rejected-during-validation"){
        rep::Assets assets(root,true),rejecter(assets.index());std::latch entered(1),release(1);rep::validationEntered=&entered;rep::validationRelease=&release;rep::validationBlocked=false;
        auto validating=std::async(std::launch::async,[&]{return assets.frame("sprite/valid/frame.img",0);});entered.wait();
        bool failed=false;try{rejecter.frame("sprite/valid/frame.img",99,0,false);}catch(const rep::Error&){failed=true;}
        auto revision=rejecter.sourceRevision();release.count_down();auto frame=validating.get();rep::validationEntered=rep::validationRelease=nullptr;
        expect(failed&&revision==1,"concurrent invalid frame did not reject its whole patch path");expect(frame->texture->rgba[0]==10,"validation completion republished a concurrently rejected patch");
    }else if(mode==L"patch-nonblocking"){
        rep::Assets assets(root,true),warm(assets.index());auto frame=warm.frame("sprite/cache0/frame.img",0);auto expected=rep::crc(frame->texture->rgba);frame.reset();
        std::latch entered(1),release(1);rep::validationEntered=&entered;rep::validationRelease=&release;rep::validationBlocked=false;
        auto validation=std::async(std::launch::async,[&]{return assets.frame("sprite/valid/frame.img",0);});entered.wait();
        auto hot=std::async(std::launch::async,[&]{return rep::crc(warm.frame("sprite/cache0/frame.img",0)->texture->rgba);});
        auto immediate=hot.wait_for(std::chrono::milliseconds(250))==std::future_status::ready;
        release.count_down();validation.get();auto actual=hot.get();rep::validationEntered=rep::validationRelease=nullptr;
        expect(immediate,"whole-IMG validation blocks another reader's hot frame/revision check");expect(actual==expected,"concurrent hot IMG pixels changed");
    }else if(mode==L"gpu"){
        rep::Gpu gpu(root);auto a=pixels(1),b=pixels(2),c=pixels(3);auto ta=gpu.texture(a);std::weak_ptr<rep::GpuTexture> wa=ta;ta.reset();auto one=gpuBytes(gpu);expect(one>=a->rgba.capacity()*2,"GPU cache omits CPU shadow or GPU allocation");
        auto tb=gpu.texture(b);std::weak_ptr<rep::GpuTexture> wb=tb;tb.reset();auto tc=gpu.texture(c);std::weak_ptr<rep::GpuTexture> wc=tc;tc.reset();gpu.texture(a);auto count=gpu.uploads;pruneGpu(gpu,one*2);expect(!wa.expired()&&wb.expired()&&!wc.expired(),"GPU LRU eviction did not preserve recent textures");expect(gpuBytes(gpu)<=one*2,"GPU cache exceeds byte budget");gpu.texture(a);expect(gpu.uploads==count,"retained GPU texture was uploaded again");gpu.texture(b);expect(gpu.uploads==count+1,"evicted GPU texture did not reload");
        auto pinned=gpu.texture(c);pruneGpu(gpu,0);expect(!wc.expired()&&gpuBytes(gpu)==0,"prune invalidated an externally pinned texture");pinned.reset();expect(wc.expired(),"evicted GPU texture remains owned after final external release");auto old=gpu.texture(a);std::weak_ptr<rep::GpuTexture> cleared=old;old.reset();gpu.resetTextures();expect(cleared.expired(),"resetTextures no longer fully clears");
    }else throw std::runtime_error("unknown test mode");std::cout<<"{\"mode\":\""<<rep::utf8(argv[1])<<"\",\"passed\":true}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
