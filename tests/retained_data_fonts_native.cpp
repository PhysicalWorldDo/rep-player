#include "fonts.hpp"
#include "protocol.hpp"
#include <future>
#include <fstream>
#include <iostream>
#include <type_traits>
#if __has_include("retained_cache.hpp")
#include "retained_cache.hpp"
#define HAVE_RETAINED_CACHE 1
#else
namespace rep {
class RetainedCache {public:
    struct Stats {size_t retainedBytes=0,inactiveBytes=0,activeBytes=0,entries=0,evictions=0,budget=0;uint64_t hits=0,misses=0;};
    explicit RetainedCache(size_t=256*1024*1024){}
    template<class T>std::shared_ptr<const T> find(const std::string&){return {};}
    template<class T>void insert(std::string,std::shared_ptr<const T>,size_t){}
    void trim(){}void trim(size_t){}void clear(){}void erasePrefix(const std::string&){}Stats stats()const{return {};}
};
class ReplayCache {public:ReplayCache(std::shared_ptr<RetainedCache>,std::string={}){}std::unique_ptr<Replay> open(const std::filesystem::path& p,ReplayOptions o={},const std::function<bool()>& c={}){return std::make_unique<Replay>(p,o,c);}};
}
#endif
static void require(bool ok,const char* message){if(!ok)throw rep::Error(message);}
template<class T>auto rasterizer(const std::filesystem::path& root,std::shared_ptr<rep::RetainedCache> cache){
    if constexpr(std::is_constructible_v<T,std::filesystem::path,std::shared_ptr<rep::RetainedCache>>)return std::make_unique<T>(root,cache);
    else return std::make_unique<T>(root);
}
template<class T>size_t replayBytes(const T& replay){if constexpr(requires{replay.estimatedDataBytes();})return replay.estimatedDataBytes();else return 0;}
template<class T>std::shared_ptr<const void> identity(const T& replay){if constexpr(requires{replay.dataIdentity();})return replay.dataIdentity();else return {};}
template<class T>void demandHook(T& rasterizer,std::function<void()> hook){if constexpr(requires{rasterizer.setDemandWaitCallback(hook);})rasterizer.setDemandWaitCallback(std::move(hook));}
int wmain(int argc,wchar_t** argv){try{
    if(argc<3)return 2;std::wstring mode=argv[1];std::filesystem::path input=argv[2];
    auto cache=std::make_shared<rep::RetainedCache>(64*1024*1024);
    if(mode==L"budget"){
        auto item=std::make_shared<const rep::Bytes>(96,7);cache->insert<rep::Bytes>("a",item,96);cache->insert<rep::Bytes>("alias",item,96);
        require(cache->stats().retainedBytes==96,"shared cache owners must be counted once");
        cache->trim(64);require(cache->stats().activeBytes==96&&cache->stats().entries==2,"externally owned data must stay pinned above retention budget");
        auto held=cache->find<rep::Bytes>("alias");require(held==item,"cache lookup lost typed owner");item.reset();cache->trim();require(cache->stats().entries==2,"lookup owner must keep data active");
        held.reset();cache->trim();require(cache->stats().entries==0&&cache->stats().retainedBytes==0,"inactive aliases must evict as one owner");
        cache->trim(4);auto a=std::make_shared<const int>(1),b=std::make_shared<const int>(2);cache->insert<int>("lru:a",a,4);a.reset();cache->insert<int>("lru:b",b,4);b.reset();cache->trim();
        require(!cache->find<int>("lru:a")&&cache->find<int>("lru:b"),"retention must evict least recently used inactive owner");
        auto survivor=cache->find<int>("lru:b");cache->clear();require(*survivor==2&&cache->stats().entries==0,"cache clear must preserve external owner validity");
    }else if(mode==L"replay"){
        rep::ReplayCache replays(cache,"client/revision1");auto a=replays.open(input);rep::Scene first;require(a->next(first)&&first.ordinal==0,"fixture cursor start");
        auto b=replays.open(input);require(&a->dictionary==&b->dictionary,"cached open decoded a second dictionary");require(identity(*a)==identity(*b)&&identity(*a),"replay data owner identity must be public and shared");require(replayBytes(*a)>a->dictionary.size()*sizeof(rep::Command),"replay byte estimate must include nested parsed storage");
        auto canonicalAlias=replays.open(input.parent_path()/L"."/input.filename());require(identity(*canonicalAlias)==identity(*b),"equivalent filesystem paths must use canonical identity");
        require(b->next(first)&&first.ordinal==0,"cached replay must get independent cursor");a.reset();require(b->next(first)&&first.ordinal==1,"cached data outlives original cursor");
        rep::ReplayOptions raw;raw.decodeResourceStrings=false;auto c=replays.open(input,raw);require(&c->dictionary!=&b->dictionary,"parse options must partition cache");
        raw={};raw.resourceCodePage=936;auto d=replays.open(input,raw);require(&d->dictionary!=&b->dictionary,"explicit codepage must partition cache");
        raw={};raw.profile=rep::ProtocolProfile::DnfCompatible;auto e=replays.open(input,raw);require(&e->dictionary!=&b->dictionary,"profile must partition cache");
        rep::ReplayCache other(cache,"client/revision2");auto f=other.open(input);require(&f->dictionary!=&b->dictionary,"applied client domain must partition cache");
        bool cancelled=false;try{replays.open(input,{},[]{return true;});}catch(const rep::Error& ex){cancelled=std::string(ex.what()).find("cancelled")!=std::string::npos;}require(cancelled,"cache hit must still honor cancellation");
        auto before=identity(*b);auto oldTime=std::filesystem::last_write_time(input);std::filesystem::last_write_time(input,oldTime+std::chrono::seconds(2));auto changed=replays.open(input);require(identity(*changed)!=before,"modified file must not reuse parsed data");
        auto renamed=input;renamed+=L".original";std::filesystem::rename(input,renamed);std::filesystem::copy_file(renamed,input);std::filesystem::last_write_time(input,oldTime+std::chrono::seconds(2));auto replaced=replays.open(input);require(identity(*replaced)!=identity(*changed),"same size/time replacement must miss by file identity");
    }else if(mode==L"replay-validation"){
        rep::ReplayCache replays(cache,"validation");size_t polls=0;bool cancelled=false;try{replays.open(input,{},[&]{return ++polls>=20;});}catch(const rep::Error& ex){cancelled=std::string(ex.what()).find("cancelled")!=std::string::npos;}require(cancelled&&cache->stats().entries==0,"cancelled dictionary parse must never enter retained cache");
        auto bad=input;bad+=L".bad.rep";auto bytes=rep::readFile(input);bytes[0]^=1;{std::ofstream out(bad,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
        bool rejected=false;try{replays.open(bad);}catch(const rep::Error& ex){rejected=std::string(ex.what()).find("CRC32")!=std::string::npos;}require(rejected&&cache->stats().entries==0,"first cache admission must preserve full payload CRC validation");auto good=replays.open(input);auto validation=good->validate();require(validation.scenes==2&&validation.exactEof,"failed/cancelled admission poisoned later valid replay");
    }else if(mode==L"font-demand"){
        size_t waits=0;auto a=rasterizer<rep::Rasterizer>(input,cache);demandHook(*a,[&]{++waits;});auto glyph=a->glyph(2,18,400,0,0,'A',2);require(waits>0,"true font/glyph miss must notify demand wait");auto before=waits;require(a->glyph(2,18,400,0,0,'A',2)==glyph&&waits==before,"instance glyph cache hit must not notify wait");a.reset();auto b=rasterizer<rep::Rasterizer>(input,cache);demandHook(*b,[&]{++waits;});require(b->glyph(2,18,400,0,0,'A',2)==glyph&&waits==before,"shared glyph cache hit must not notify wait");
    }else if(mode==L"fonts"){
        auto a=rasterizer<rep::Rasterizer>(input,cache);auto ga=a->glyph(2,18,400,0,0,'A',2);auto metrics=std::tuple{ga->width,ga->height,ga->left,ga->top,ga->advance,ga->coverage};a.reset();
        auto b=rasterizer<rep::Rasterizer>(input,cache);auto gb=b->glyph(2,18,400,0,0,'A',2);require(ga==gb,"glyph coverage/metrics must survive Rasterizer replacement");
        require(metrics==std::tuple{gb->width,gb->height,gb->left,gb->top,gb->advance,gb->coverage},"cached glyph values changed");
        require(b->glyph(2,18,400,0,0,'A',3)!=gb,"metric outline must remain in glyph cache key");require(b->glyph(2,18,400,0,1,'A',2)!=gb,"outline must remain in glyph cache key");require(b->glyph(2,19,400,0,0,'A',2)!=gb,"font height must remain in glyph cache key");require(b->glyph(2,18,700,0,0,'A',2)!=gb,"font weight must remain in glyph cache key");require(b->glyph(2,18,400,1,0,'A',2)!=gb,"font slant must remain in glyph cache key");require(b->glyph(2,18,400,0,0,'B',2)!=gb,"character must remain in glyph cache key");
        auto c=rasterizer<rep::Rasterizer>(input.parent_path()/L"different"/L"Fonts",cache);require(c->glyph(2,18,400,0,0,'A',2)!=gb,"different font root cannot share registered glyph result");
        auto one=std::async(std::launch::async,[&]{auto r=rasterizer<rep::Rasterizer>(input,cache);return r->glyph(2,18,400,0,0,'A',2);});auto two=std::async(std::launch::async,[&]{auto r=rasterizer<rep::Rasterizer>(input,cache);return r->glyph(2,18,400,0,0,'A',2);});require(one.get()==gb&&two.get()==gb,"parallel independent faces must share immutable glyph output");
        auto stats=cache->stats();require(stats.retainedBytes>5000000&&stats.entries>2,"font bytes and glyphs must be retained and accounted");cache->clear();require(gb->coverage==ga->coverage,"font cache clear invalidated active glyph data");
        require(b->glyph(2,18,400,0,0,'C',2)->advance>0,"clearing font cache freed memory still used by an independent FTface");
    }else if(mode==L"font-change"){
        auto a=rasterizer<rep::Rasterizer>(input,cache);auto ga=a->glyph(2,18,400,0,0,'A',2);auto file=input/L"DNFForgedBlade-Medium.ttf";auto time=std::filesystem::last_write_time(file);std::filesystem::last_write_time(file,time+std::chrono::seconds(2));auto b=rasterizer<rep::Rasterizer>(input,cache);require(b->glyph(2,18,400,0,0,'A',2)!=ga,"changed font source identity must invalidate cached coverage");
        auto cn=input.parent_path()/L"DNF.exe";{std::ofstream marker(cn);marker<<"fixture";}auto c=rasterizer<rep::Rasterizer>(input,cache);require(c->familyKind(2)!=a->familyKind(2),"native font registry priority must follow selected client");require(c->glyph(2,18,400,0,0,'A',2)!=ga,"different native font registry must not share glyph result");
    }else return 2;
    std::cout<<"{\"ok\":true,\"mode\":\""<<rep::utf8(mode)<<"\"}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
