#include "resources.hpp"
#include <algorithm>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <thread>

namespace rep {
namespace {
using ResourceCancelled=ImagePreparationCancelled;
thread_local std::function<bool()> resourceCancellation;
struct ResourceCancellationScope {
    std::function<bool()> previous;
    explicit ResourceCancellationScope(std::function<bool()> callback):previous(std::move(resourceCancellation)){resourceCancellation=std::move(callback);}
    ~ResourceCancellationScope(){resourceCancellation=std::move(previous);}
};
void checkResourceCancelled(const std::function<bool()>& callback){if(callback&&callback())throw ResourceCancelled{};}
auto lockImage(std::recursive_timed_mutex& mutex,const std::function<bool()>& cancelled){
    std::unique_lock<std::recursive_timed_mutex> lock(mutex,std::defer_lock);
    while(!lock.try_lock_for(std::chrono::milliseconds(2)))checkResourceCancelled(cancelled);
    checkResourceCancelled(cancelled);return lock;
}
}
std::shared_ptr<Pixels> nativeImageTexture(std::shared_ptr<Pixels> pixels){
    int w=pixels->width,h=pixels->height;pixels->logicalWidth=w;pixels->logicalHeight=h;
    if(w<=512&&h<=512){auto power=[](int n){int p=16;while(p<n)p*=2;return p;};int pw=power(w),ph=power(h);
        if(pw!=w||ph!=h){Bytes padded(size_t(pw)*ph*4);for(int y=0;y<h;y++)std::copy_n(pixels->rgba.data()+size_t(y)*w*4,w*4,padded.data()+size_t(y)*pw*4);pixels->width=pw;pixels->height=ph;pixels->rgba=std::move(padded);}}
    return pixels;
}
static std::shared_ptr<Pixels> rawPixels(std::span<const uint8_t> raw,int fmt,int w,int h,const Bytes* palette=nullptr) {
    if(w<=0||h<=0)throw Error("invalid IMG dimensions");
    auto out=std::make_shared<Pixels>();out->width=w;out->height=h;out->rgba.resize(size_t(w)*h*4);
    auto& b=out->rgba;
    if(palette) {
        if(raw.size()!=size_t(w)*h)throw Error("invalid palette pixel length");
        for(size_t p=0;p<raw.size();p++){size_t ix=size_t(raw[p])*4;if(ix+4>palette->size())throw Error("palette index out of range");std::memcpy(b.data()+p*4,palette->data()+ix,4);}
    } else if(fmt==16) {
        // Patch payloads may inflate to more than the declared image needs.
        // Decode only width*height pixels, as dnflib and KoishiEx do.
        if(raw.size()<b.size())throw Error("invalid BGRA pixel length");
        for(size_t p=0;p<b.size();p+=4){b[p]=raw[p+2];b[p+1]=raw[p+1];b[p+2]=raw[p];b[p+3]=raw[p+3];}
    } else if(fmt==14||fmt==15) {
        if(raw.size()<size_t(w)*h*2)throw Error("invalid 16-bit pixel length");
        for(size_t p=0;p<size_t(w)*h;p++){auto x=at<uint16_t>(raw,p*2);
            if(fmt==14){for(int c=0;c<3;c++){int n=(x>>(10-c*5))&31;b[p*4+c]=uint8_t((n<<3)|(n>>2));}b[p*4+3]=(x>>15)*255;}
            else {int shifts[]={8,4,0,12};for(int c=0;c<4;c++)b[p*4+c]=uint8_t(((x>>shifts[c])&15)*17);}
        }
    } else if(fmt>=18&&fmt<=20) {
        if(raw.size()<128||std::memcmp(raw.data(),"DDS ",4))throw Error("missing DDS header");
        if(at<uint32_t>(raw,12)!=uint32_t(h)||at<uint32_t>(raw,16)!=uint32_t(w))throw Error("DDS dimensions disagree with IMG");
        uint32_t fourcc=at<uint32_t>(raw,84);int mode=fourcc==0x31545844?1:fourcc==0x33545844?2:fourcc==0x35545844?3:0;
        if(!mode)throw Error("unsupported DDS compression");size_t offset=128;
        for(int by=0;by<h;by+=4)for(int bx=0;bx<w;bx+=4) {
            size_t stride=mode==1?8:16;if(offset+stride>raw.size())throw Error("truncated DDS block");auto block=raw.subspan(offset,stride);offset+=stride;
            size_t co=mode==1?0:8;uint16_t a=at<uint16_t>(block,co),d=at<uint16_t>(block,co+2);uint32_t indices=at<uint32_t>(block,co+4);
            std::array<std::array<uint8_t,4>,4> colors{};
            for(int k=0;k<2;k++){uint16_t n=k?d:a;int r=(n>>11)&31,g=(n>>5)&63,blue=n&31;colors[k]={uint8_t((r<<3)|(r>>2)),uint8_t((g<<2)|(g>>4)),uint8_t((blue<<3)|(blue>>2)),255};}
            if(a>d||mode!=1){for(int c=0;c<3;c++){colors[2][c]=(2*colors[0][c]+colors[1][c])/3;colors[3][c]=(colors[0][c]+2*colors[1][c])/3;}colors[2][3]=colors[3][3]=255;}
            else {for(int c=0;c<3;c++)colors[2][c]=(colors[0][c]+colors[1][c])/2;colors[2][3]=255;colors[3]={0,0,0,0};}
            std::array<uint8_t,8> alpha{};uint64_t abits=0;
            if(mode==3){alpha[0]=block[0];alpha[1]=block[1];if(alpha[0]>alpha[1])for(int k=2;k<8;k++)alpha[k]=((8-k)*alpha[0]+(k-1)*alpha[1])/7;
                else {for(int k=2;k<6;k++)alpha[k]=((6-k)*alpha[0]+(k-1)*alpha[1])/5;alpha[6]=0;alpha[7]=255;}for(int k=0;k<6;k++)abits|=uint64_t(block[k+2])<<(k*8);}
            for(int p=0;p<16;p++){int x=bx+p%4,y=by+p/4;if(x>=w||y>=h)continue;auto color=colors[(indices>>(2*p))&3];
                if(mode==2)color[3]=uint8_t(((block[p/2]>>((p%2)*4))&15)*17);else if(mode==3)color[3]=alpha[(abits>>(p*3))&7];
                std::memcpy(b.data()+(size_t(y)*w+x)*4,color.data(),4);}
        }
    } else throw Error("unsupported IMG color format "+std::to_string(fmt));
    return out;
}
Bytes Frame::cropped() const {
    Bytes out(size_t(width)*height*4);if(empty)return out;
    for(int y0=0;y0<height;y0++)for(int x0=0;x0<width;x0++) {
        int x=rotated?rect[2]-1-y0:rect[0]+x0,y=rotated?rect[1]+x0:rect[1]+y0;
        x=std::clamp(x,0,texture->width-1);y=std::clamp(y,0,texture->height-1);
        std::memcpy(out.data()+(size_t(y0)*width+x0)*4,texture->rgba.data()+(size_t(y)*texture->width+x)*4,4);
    }
    return out;
}
Img::Img(Bytes data):data_(std::move(data)) {
    Reader r(data_);auto magic=r.take(16);uint32_t indexBytes=0,n=0;
    if(!std::memcmp(magic.data(),"Neople Img File\0",16)){indexBytes=r.get<uint32_t>();r.take(4);version=r.get<uint32_t>();n=r.get<uint32_t>();}
    else if(data_.size()>=32&&!std::memcmp(data_.data(),"Neople Image File\0",18)){r.pos=20;r.take(4);version=r.get<uint32_t>();n=r.get<uint32_t>();}
    else throw Error("invalid IMG magic");
    if(version!=1&&version!=2&&version!=4&&version!=5&&version!=6)throw Error("unsupported IMG version");
    uint32_t atlasCount=0;if(version==5){atlasCount=r.get<uint32_t>();r.take(4);}
    uint32_t palettes=(version==4||version==5)?1:version==6?r.get<uint32_t>():0;
    while(palettes--){auto count=r.get<uint32_t>();auto b=r.take(size_t(count)*4);palettes_.emplace_back(b.begin(),b.end());}
    while(atlasCount--){std::vector<int32_t> a;for(int k=0;k<7;k++)a.push_back(r.get<int32_t>());atlases_.push_back(std::move(a));}
    auto start=r.pos;
    while(n--){std::vector<int32_t> a;a.push_back(r.get<int32_t>());int len=a[0]==17?2:9;
        for(int k=1;k<len;k++)a.push_back(r.get<int32_t>());
        if(a[0]!=17){if(a[1]==7)for(int k=0;k<7;k++)a.push_back(r.get<int32_t>());else if(a[1]!=5&&a[1]!=6)throw Error("unsupported IMG compression");}
        if(version==1){offsets_.push_back(r.pos);if(a[0]!=17){if(a[1]==5)a[4]=a[2]*a[3]*(a[0]==16?4:2);if(a[4]<0)throw Error("negative IMG pixel length");r.take(a[4]);}}
        records_.push_back(std::move(a));}
    if(version!=1&&indexBytes&&r.pos-start!=indexBytes)throw Error("IMG frame table length mismatch");
    for(auto& a:atlases_){atlasOffsets_.push_back(r.pos);if(a[3]<0)throw Error("negative atlas length");r.take(a[3]);}
    if(version!=1)for(size_t index=0;index<records_.size();++index){
        auto& a=records_[index];offsets_.push_back(r.pos);
        if(a[0]==17||a[1]==7)continue;
        if(a[1]==5)a[4]=a[2]*a[3]*(a[0]==16?4:2);
        if(a[4]<0)throw Error("negative IMG pixel length");
        // Native empty frames never read pixels. Some v2 patches omit the
        // final raw pixel slot; linked frames still share the empty frame.
        // Existing padding and any subsequent data-bearing frame stay strict.
        const bool omittedEmptyTail=version==2&&a[1]==5&&(a[0]==14||a[0]==15||a[0]==16)
            &&a[2]==1&&a[3]==1&&a[5]==a[7]&&a[6]==0&&r.remaining()==0
            &&std::all_of(records_.begin()+index+1,records_.end(),[](const auto& next){return next[0]==17;});
        if(!omittedEmptyTail)r.take(a[4]);
    }
}
std::shared_ptr<Pixels> Img::atlas(int index) {
    if(auto it=atlasTextures_.find(index);it!=atlasTextures_.end())return it->second;
    if(index<0||size_t(index)>=atlases_.size())throw Error("atlas index out of range");
    const auto& a=atlases_[index];auto raw=inflateAll(std::span(data_).subspan(atlasOffsets_[index],a[3]));
    if(raw.size()!=size_t(a[4]))throw Error("atlas raw length mismatch");return atlasTextures_[index]=nativeImageTexture(rawPixels(raw,a[1],a[5],a[6]));
}
std::shared_ptr<Frame> Img::frameInner(int index,int palette,std::unordered_set<int>& visiting) {
    checkResourceCancelled(resourceCancellation);
    if(index<0||size_t(index)>=records_.size())throw std::out_of_range("IMG frame out of range");
    // CN 148C38C10 / 148C38CC0 load one palette for v4/v5;
    // the filename selector is only meaningful for the v6 palette table.
    if(version==4||version==5)palette=0;
    uint64_t key=(uint64_t(uint32_t(index))<<32)|uint32_t(palette);if(auto it=frames_.find(key);it!=frames_.end())return it->second;
    if(!visiting.insert(index).second)throw Error("linked IMG frame cycle");
    const auto& a=records_[index];if(a[0]==17)return frames_[key]=frameInner(a[1],palette,visiting);
    auto f=std::make_shared<Frame>();f->x=a[5];f->y=a[6];f->width=a[2];f->height=a[3];f->fullWidth=a[7];f->fullHeight=a[8];
    if(f->width<=0||f->height<=0)throw Error("invalid frame dimensions");
    f->empty=f->width<2&&f->height<2&&f->x==f->fullWidth&&f->y==0;
    if(f->empty){f->texture=std::make_shared<Pixels>();f->texture->width=f->width;f->texture->height=f->height;f->texture->rgba.resize(4);f->rect={0,0,f->width,f->height};}
    else if(a[1]==7){f->atlas=true;f->texture=atlas(a[10]);f->rect={a[11],a[12],a[13],a[14]};f->rotated=a[15]!=0;
        if(a[15]!=0&&a[15]!=1)throw Error("invalid atlas rotation");
        if(f->rect[0]<0||f->rect[1]<0||f->rect[2]>f->texture->width||f->rect[3]>f->texture->height)throw Error("atlas crop out of range");
        int w=f->rect[2]-f->rect[0],h=f->rect[3]-f->rect[1];if((f->rotated?h:w)!=f->width||(f->rotated?w:h)!=f->height)throw Error("atlas crop dimensions mismatch");}
    else {auto s=std::span(data_).subspan(offsets_[index],a[4]);Bytes decoded;if(a[1]==6){decoded=inflateAll(s);s=decoded;}
        const Bytes* p=nullptr;
        if(a[1]==6&&a[0]==14&&std::any_of(palettes_.begin(),palettes_.end(),[](const auto& b){return !b.empty();})) {
            if(palette<0||size_t(palette)>=palettes_.size())throw Error("IMG palette index out of range");p=&palettes_[palette];if(p->empty())throw Error("empty IMG palette");}
        f->texture=nativeImageTexture(rawPixels(s,a[0],f->width,f->height,p));f->rect={0,0,f->width,f->height};}
    return frames_[key]=f;
}
std::shared_ptr<Frame> Img::frame(int index,int palette,const std::function<bool()>& cancelled){auto lock=lockImage(mutex_,cancelled);ResourceCancellationScope scope(cancelled);std::unordered_set<int> visiting;return frameInner(index,palette,visiting);}
void Img::validateFrames(){
    auto cancelled=resourceCancellation;validateFrames(cancelled);
}
void Img::validateFrames(const std::function<bool()>& cancelled){
    auto lock=lockImage(mutex_,cancelled);ResourceCancellationScope scope(cancelled);
    for(size_t i=0;i<records_.size();++i){
        checkResourceCancelled(cancelled);
        if(records_[i][0]==17){
            std::unordered_set<int> visiting;int target=int(i);
            while(true){if(target<0||size_t(target)>=records_.size())throw Error("IMG link target out of range");
                if(!visiting.insert(target).second)throw Error("linked IMG frame cycle");
                if(records_[target][0]!=17)break;target=records_[target][1];}
        }else{frame(int(i),0,cancelled);frames_.clear();}
    }
    atlasTextures_.clear();
}
static std::string normalize(std::string name) {name=canonical(name);while(!name.empty()&&name[0]=='/')name.erase(0,1);if(name.rfind("sprite/",0)!=0)name="sprite/"+name;return name;}
struct Assets::Index {
    std::filesystem::path root;
    bool patchesEnabled=true;
    std::unordered_map<std::string,Entry> entries,originals;
    std::unordered_set<std::string> packages;
    // Package mappings stay immutable; health decisions follow the applied
    // index across playback, thumbnails and new export readers.
    struct RejectedPatches {std::mutex mutex;std::unordered_set<std::string> paths,validated;uint64_t revision=0;};
    std::shared_ptr<RejectedPatches> rejected=std::make_shared<RejectedPatches>();
};
struct Assets::PendingImage {
    std::mutex mutex;std::condition_variable condition;bool done=false;
    uint64_t revision=0;std::shared_ptr<Img> image;std::exception_ptr error;
};
struct Assets::Prefetch {
    using Clock=std::chrono::steady_clock;
    struct Job {
        FrameKey key;int priority=2;uint64_t generation=0,epoch=0,revision=0;
        std::function<bool()> cancelled;Clock::time_point queuedAt=Clock::now();
        enum State {Pending,Running,Done} state=Pending;
        std::mutex mutex;std::condition_variable condition;bool done=false,aborted=false;
        std::weak_ptr<Frame> frame;std::exception_ptr error;
        std::weak_ptr<Img> image;
    };
    Assets& owner;std::mutex mutex;std::condition_variable condition;
    std::atomic<bool> quitting=false;std::atomic<uint64_t> epoch=1;uint64_t generation=0;
    std::array<std::deque<std::shared_ptr<Job>>,3> queues;
    std::unordered_map<FrameKey,std::shared_ptr<Job>,FrameKeyHash> jobs;
    std::deque<std::shared_ptr<Job>> ready;
    PrefetchStats stats;std::thread thread;
    explicit Prefetch(Assets& assets):owner(assets),thread([this]{run();}){}
    ~Prefetch(){quitting=true;epoch++;condition.notify_all();if(thread.joinable())thread.join();}
    void cancelLocked(){
        epoch++;ready.clear();
        for(auto& [key,job]:jobs)if(job->state==Job::Pending){
            job->state=Job::Done;{std::lock_guard lock(job->mutex);job->done=job->aborted=true;}
            job->condition.notify_all();stats.cancelled++;if(stats.pendingJobs)stats.pendingJobs--;
        }
        for(auto& queue:queues)queue.clear();jobs.clear();
    }
    void run(){
        while(!quitting){
            std::shared_ptr<Job> job;
            {std::unique_lock lock(mutex);condition.wait(lock,[&]{return quitting||!queues[0].empty()||!queues[1].empty()||!queues[2].empty();});
                if(quitting)break;
                for(int priority=0;priority<3&&!job;priority++)while(!queues[priority].empty()){
                    auto candidate=std::move(queues[priority].front());queues[priority].pop_front();
                    if(candidate->state==Job::Pending&&candidate->priority==priority){job=std::move(candidate);job->state=Job::Running;break;}
                }}
            if(!job)continue;
            const auto began=Clock::now();std::shared_ptr<Frame> frame;std::shared_ptr<Img> image;std::exception_ptr error;bool aborted=false;
            auto cancelled=[this,job]{return quitting.load()||epoch.load()!=job->epoch||(job->cancelled&&job->cancelled());};
            try{checkResourceCancelled(cancelled);if(owner.sourceRevision()!=job->revision)throw ResourceCancelled{};
                if(job->key.index<0)image=owner.preloadInner(job->key.logical,cancelled);
                else frame=owner.frameInner(job->key.logical,job->key.index,job->key.palette,true,cancelled);
                checkResourceCancelled(cancelled);
            }catch(const ResourceCancelled&){aborted=true;}catch(...){error=std::current_exception();}
            auto revision=owner.sourceRevision();auto ended=Clock::now();
            aborted|=cancelled();
            {std::lock_guard lock(mutex);aborted|=quitting.load()||epoch.load()!=job->epoch;job->state=Job::Done;
                {std::lock_guard resultLock(job->mutex);job->frame=frame;job->image=image;job->revision=revision;job->error=error;job->aborted=aborted;job->done=true;}
                if(stats.pendingJobs)stats.pendingJobs--;stats.queueMilliseconds+=std::chrono::duration<double,std::milli>(began-job->queuedAt).count();
                auto milliseconds=std::chrono::duration<double,std::milli>(ended-began).count();stats.jobMilliseconds+=milliseconds;stats.maxJobMilliseconds=std::max(stats.maxJobMilliseconds,milliseconds);
                if(aborted)stats.cancelled++;else if(error)stats.failed++;else {stats.completed++;ready.push_back(job);}
            }
            job->condition.notify_all();frame.reset();image.reset();
            // This is the active replay's working set. Its existing Pixels may
            // already be held by GPU/material entries, so do not evict/redecode
            // them while warming. The controller prunes retention at REP changes.
        }
    }
};
Assets::Assets(std::filesystem::path root,bool patchesEnabled):Assets(buildIndex(std::move(root),patchesEnabled)){}
Assets::Assets(IndexPtr index):index_(std::move(index)){
    if(!index_)throw Error("resource index is absent");
    root_=index_->root;
}
Assets::~Assets(){prefetch_.reset();}
Assets::Prefetch& Assets::prefetchState(){if(!prefetch_)prefetch_=std::make_unique<Prefetch>(*this);return *prefetch_;}
void Assets::prefetch(const std::vector<FrameRequest>& requests,uint64_t generation,const std::function<bool()>& cancelled){
    auto& state=prefetchState();auto revision=sourceRevision();std::vector<FrameKey> retouch;
    {std::lock_guard lock(state.mutex);
    if(state.generation!=generation){state.cancelLocked();state.generation=generation;}
    for(const auto& request:requests){if(request.path.empty())continue;FrameKey key{normalize(request.path),request.index,request.palette};
        auto found=state.jobs.find(key);auto priority=std::clamp(request.priority,0,2);
        if(found!=state.jobs.end()){
            auto job=found->second;
            if(job->revision==revision&&job->epoch==state.epoch){
                if(job->state==Prefetch::Job::Pending&&priority<job->priority){job->priority=priority;state.queues[priority].push_back(job);}
                if(job->state!=Prefetch::Job::Done)continue;
                std::lock_guard resultLock(job->mutex);if(job->error)continue;
                if(!job->frame.expired()||!job->image.expired()){if(priority<2)retouch.push_back(job->key);continue;}
            }
        }
        auto job=std::make_shared<Prefetch::Job>();job->key=std::move(key);job->priority=priority;job->generation=generation;job->epoch=state.epoch;job->revision=revision;job->cancelled=cancelled;
        state.jobs[job->key]=job;state.queues[priority].push_back(job);state.stats.queued++;state.stats.pendingJobs++;
    }
    state.condition.notify_one();}
    // Rolling lookahead is reuse, just like a foreground hit. Refresh its real
    // source after releasing the queue lock so source invalidation cannot invert
    // queue/cache lock order. No additional strong references pin the window.
    if(!retouch.empty()){std::lock_guard lock(mutex_);syncSources();for(const auto& key:retouch){auto found=resolvedFrames_.find(key);if(found!=resolvedFrames_.end())touchImage(found->second.source);else if(key.index<0)touchImage(key.logical);}}
}
void Assets::cancelPrefetch(){if(prefetch_){std::lock_guard lock(prefetch_->mutex);prefetch_->cancelLocked();prefetch_->condition.notify_all();}}
std::vector<std::shared_ptr<Frame>> Assets::drainReady(uint64_t generation,size_t maxCount){
    std::vector<std::shared_ptr<Frame>> result;if(!prefetch_)return result;auto revision=sourceRevision();std::lock_guard lock(prefetch_->mutex);
    if(prefetch_->generation!=generation)return result;
    while(!prefetch_->ready.empty()&&result.size()<maxCount){auto job=std::move(prefetch_->ready.front());prefetch_->ready.pop_front();
        if(job->epoch==prefetch_->epoch&&job->revision==revision)if(auto frame=job->frame.lock())result.push_back(std::move(frame));}
    return result;
}
Assets::PrefetchStats Assets::prefetchStats()const{if(!prefetch_)return {};std::lock_guard lock(prefetch_->mutex);auto result=prefetch_->stats;result.readyFrames=prefetch_->ready.size();return result;}
bool Assets::patchesEnabled()const{return index_->patchesEnabled;}
Assets::IndexPtr Assets::buildIndex(std::filesystem::path root,bool patchesEnabled){
    auto index=std::make_shared<Index>();index->root=std::move(root);index->patchesEnabled=patchesEnabled;
    if(!patchesEnabled){indexNative(*index);return index;}
    std::vector<std::wstring> names;
    if(std::filesystem::is_directory(index->root))for(const auto& file:std::filesystem::directory_iterator(index->root)){
        if(!file.is_regular_file())continue;
        auto extension=file.path().extension().native();
        for(auto& c:extension)if(c>=L'A'&&c<=L'Z')c+=32;
        if(extension==L".npk")names.push_back(file.path().filename().native());
    }
    std::sort(names.begin(),names.end(),[](const std::wstring& a,const std::wstring& b){
        return std::lexicographical_compare(a.begin(),a.end(),b.begin(),b.end(),[](wchar_t x,wchar_t y){return uint16_t(x)<uint16_t(y);});
    });
    const auto boundary=std::find_if(names.begin(),names.end(),[](const std::wstring& name){
        constexpr std::wstring_view prefix=L"sprite";
        if(name.size()<prefix.size())return false;
        for(size_t k=0;k<prefix.size();++k){auto c=name[k];if(c>=L'A'&&c<=L'Z')c+=32;if(c!=prefix[k])return false;}
        return true;
    });
    // The naming convention limits overlay scans; original packages keep the
    // established ETC/guessed-package lookup and are not eagerly indexed.
    if(boundary!=names.end())for(auto it=names.begin();it!=boundary;++it){
        auto name=utf8(*it);readPackage(index->root,name,index->entries,true);index->packages.insert(std::move(name));
    }
    indexNative(*index);
    return index;
}
void Assets::indexNative(Index& index) {
    auto path=index.root/L"NpkIndex.etc";if(!std::filesystem::exists(path))return;
    auto data=readFile(path);Reader r(data);auto rawLength=r.get<uint32_t>(),zipped=r.get<uint32_t>();auto s=r.take(zipped);r.end();Bytes encoded(s.begin(),s.end());
    for(size_t k=0;k<encoded.size();k++)encoded[k]=uint8_t(uint8_t((encoded[k]^0xaa)-k*7)^0xaa);
    auto raw=inflateAll(encoded);if(raw.size()!=rawLength)throw Error("native NPK index length mismatch");Reader d(raw);
    while(d.remaining()){auto count=d.get<uint32_t>();auto n=d.get<uint32_t>();auto b=d.take(n);std::string name(reinterpret_cast<const char*>(b.data()),n);name=canonical(name);name=name.substr(name.find_last_of('/')+1);
        while(count--){n=d.get<uint32_t>();b=d.take(n);std::string logical(reinterpret_cast<const char*>(b.data()),n);auto offset=d.get<uint32_t>(),length=d.get<uint32_t>();index.originals.try_emplace(normalize(logical),Entry{name,offset,length});index.entries.try_emplace(normalize(logical),Entry{name,offset,length});}}
}
void Assets::readPackage(const std::filesystem::path& root,const std::string& name,std::unordered_map<std::string,Entry>& entries,bool patch) {try{
    std::ifstream f(root/wide(name),std::ios::binary);std::array<uint8_t,20> h{};f.read(reinterpret_cast<char*>(h.data()),20);
    if(!f||std::memcmp(h.data(),"NeoplePack_Bill\0",16))throw Error("invalid NPK magic");
    auto n=at<uint32_t>(h,16);Bytes table(size_t(n)*264);f.read(reinterpret_cast<char*>(table.data()),table.size());if(!f)throw Error("truncated NPK table");
    std::string seed="puchikon@neople dungeon and fighter ";while(seed.size()<255)seed+="DNF";seed.resize(255);seed.push_back(0);
    for(uint32_t k=0;k<n;k++){auto b=std::span(table).subspan(size_t(k)*264,264);std::string logical;for(int j=0;j<256;j++){auto c=char(b[8+j]^uint8_t(seed[j]));if(!c)break;logical.push_back(c);}entries.try_emplace(normalize(logical),Entry{name,at<uint32_t>(b,0),at<uint32_t>(b,4),patch});}
}catch(const std::exception& error){throw Error("NPK "+name+": "+error.what());}}
void Assets::indexPackage(const std::string& name) {
    {std::lock_guard lock(mutex_);if(packages_.contains(name)||index_->packages.contains(name))return;}
    if(!std::filesystem::exists(root_/wide(name)))return;
    std::unordered_map<std::string,Entry> entries;readPackage(root_,name,entries);
    std::lock_guard lock(mutex_);for(auto& [logical,entry]:entries)entries_.try_emplace(logical,std::move(entry));packages_.insert(name);
}
const Assets::Entry* Assets::resolve(std::string name) {
    name=normalize(name);
    bool rejected;{std::lock_guard lock(index_->rejected->mutex);rejected=index_->rejected->paths.contains(name);}
    if(rejected){if(auto it=index_->originals.find(name);it!=index_->originals.end())return &it->second;}
    else if(auto it=index_->entries.find(name);it!=index_->entries.end())return &it->second;
    {std::lock_guard lock(mutex_);if(auto it=entries_.find(name);it!=entries_.end())return &it->second;}
    auto parent=name.substr(0,name.find_last_of('/'));
    while(parent.find('/')!=std::string::npos){auto package=parent;std::replace(package.begin(),package.end(),'/','_');indexPackage(package+".NPK");{std::lock_guard lock(mutex_);if(auto it=entries_.find(name);it!=entries_.end())return &it->second;}parent=parent.substr(0,parent.find_last_of('/'));}
    return nullptr;
}
uint64_t Assets::sourceRevision()const{std::lock_guard lock(index_->rejected->mutex);return index_->rejected->revision;}
bool Assets::publishAtRevision(uint64_t expected,const std::function<void()>& install){
    std::lock_guard lock(index_->rejected->mutex);if(index_->rejected->revision!=expected)return false;install();return true;
}
void Assets::syncSources(){std::lock_guard lock(mutex_);auto revision=sourceRevision();if(sourceRevision_!=revision){clearDecodedImages();sourceRevision_=revision;}}
void Assets::clearDecodedImages(){cancelPrefetch();std::lock_guard lock(mutex_);resolvedFrames_.clear();images_.clear();imageTouches_.clear();}
bool Assets::patchSource(const std::string& name){
    auto entry=resolve(name);return entry&&entry->patch;
}
bool Assets::skipPatch(const std::string& name){
    if(!patchSource(name))return false;
    {std::lock_guard lock(index_->rejected->mutex);if(index_->rejected->paths.insert(name).second){++index_->rejected->revision;index_->rejected->validated.clear();}}
    syncSources();return true;
}
static std::optional<std::pair<std::string,int>> paletteImagePath(std::string name){
    // CN 148BECF00 retries an absent pack through 148BEBD10. The last
    // four-character numeric run encodes a two-digit palette in its tail:
    // katana0111b.img -> katana0100b.img, palette 11. Exact packs win.
    size_t run=0;
    for(size_t end=name.size();end>0;){
        auto c=static_cast<unsigned char>(name[--end]);
        if(c<'0'||c>';'){run=0;continue;}
        if(++run==4){
            auto pos=end+2;int palette=0;
            for(size_t i=pos;i<pos+2&&name[i]>='0'&&name[i]<='9';++i)palette=palette*10+name[i]-'0';
            auto original=name;name.replace(pos,2,"00");
            if(palette>0&&name!=original)return std::pair{std::move(name),palette};
            return {};
        }
    }
    return {};
}
std::shared_ptr<Frame> Assets::decodeFrame(const std::string& name,int index,int palette,std::string* source){
    checkResourceCancelled(resourceCancellation);
    const bool patch=patchSource(name);auto img=preloadInner(name,resourceCancellation);
    if(!img){
        if(auto alias=paletteImagePath(name);alias&&resolve(alias->first))return decodeFrame(alias->first,index,alias->second,source);
        // Newer CN UI packs may retain only the (2x) entry. Keep its pixels
        // at full resolution while drawing its geometry in logical UI units.
        // This is a compatibility lookup, independent of July palette aliases.
        if(name.starts_with("sprite/interface/")){
            auto base=name.find_last_of('/')+1;
            if(name.compare(base,4,"(2x)")!=0){auto hd=name;hd.insert(base,"(2x)");auto decoded=decodeFrame(hd,index,palette,source);
                if(!decoded)return {};auto logical=std::make_shared<Frame>(*decoded);logical->geometryScale=.5f;return logical;}
        }
        return {};
    }
    *source=name;
    if(index>=0&&size_t(index)<img->count()){
        try{return img->frame(index,palette,resourceCancellation);}
        catch(const ResourceCancelled&){throw;}
        catch(const Error&){if(patch){skipPatch(name);return decodeFrame(name,index,palette,source);}throw;}
        catch(const std::out_of_range&){if(patch){skipPatch(name);return decodeFrame(name,index,palette,source);}throw;}
    }
    if(index>=0&&patch){skipPatch(name);return decodeFrame(name,index,palette,source);}
    return {};
}
std::shared_ptr<Img> Assets::preload(std::string logical) {
    auto name=normalize(logical);{std::lock_guard lock(mutex_);syncSources();if(auto it=images_.find(name);it!=images_.end()){touchImage(name);return it->second;}}
    checkResourceCancelled(demandCancellation_);if(demandWaitCallback_)demandWaitCallback_();return preloadInner(std::move(name),demandCancellation_);
}
std::shared_ptr<Img> Assets::preloadInner(std::string logical,const std::function<bool()>& cancelled){
    auto name=normalize(std::move(logical));
    for(;;){
        checkResourceCancelled(cancelled);auto revision=sourceRevision();std::shared_ptr<PendingImage> pending;Entry entry;bool owner=false;
        {std::lock_guard lock(mutex_);syncSources();if(auto it=images_.find(name);it!=images_.end()){touchImage(name);return it->second;}}
        auto e=resolve(name);if(!e)return {};entry=*e;
        {std::lock_guard lock(mutex_);syncSources();if(auto it=images_.find(name);it!=images_.end()){touchImage(name);return it->second;}
            auto it=pendingImages_.find(name);
            if(it!=pendingImages_.end()&&it->second->revision==revision)pending=it->second;
            else {pending=std::make_shared<PendingImage>();pending->revision=revision;pendingImages_[name]=pending;owner=true;}}
        if(!owner){
            std::unique_lock lock(pending->mutex);while(!pending->done){checkResourceCancelled(cancelled);pending->condition.wait_for(lock,std::chrono::milliseconds(2));}
            auto error=pending->error;auto image=pending->image;lock.unlock();checkResourceCancelled(cancelled);
            if(error){try{std::rethrow_exception(error);}catch(const ResourceCancelled&){continue;}}
            if(sourceRevision()!=revision)continue;if(image)return image;continue;
        }
        std::shared_ptr<Img> image;std::exception_ptr error;bool retry=false;
        try{
            std::ifstream input(root_/wide(entry.package),std::ios::binary);if(!input)throw Error("NPK file absent: "+entry.package);
            Bytes bytes(entry.length);input.seekg(entry.offset);size_t offset=0;
            while(offset<bytes.size()){checkResourceCancelled(cancelled);auto count=std::min<size_t>(1048576,bytes.size()-offset);input.read(reinterpret_cast<char*>(bytes.data()+offset),std::streamsize(count));if(!input)throw Error("truncated IMG entry");offset+=count;}
            image=std::make_shared<Img>(std::move(bytes));
            if(entry.patch){
                bool rejected,validate;
                {std::lock_guard lock(index_->rejected->mutex);rejected=index_->rejected->paths.contains(name);validate=!index_->rejected->validated.contains(name);}
                if(!rejected&&validate){++patchValidations;ResourceCancellationScope scope(cancelled);image->validateFrames();}
                {std::lock_guard lock(index_->rejected->mutex);rejected=index_->rejected->paths.contains(name);if(!rejected&&index_->rejected->revision==revision&&validate)index_->rejected->validated.insert(name);}
                retry=rejected;
            }
            checkResourceCancelled(cancelled);retry|=sourceRevision()!=revision;
        }catch(const ResourceCancelled&){error=std::current_exception();}
        catch(const Error& failure){
            if(entry.patch){skipPatch(name);retry=true;}
            else error=std::make_exception_ptr(Error("IMG "+name+" in NPK "+entry.package+" (offset "+std::to_string(entry.offset)+", length "+std::to_string(entry.length)+"): "+failure.what()));
        }catch(...){error=std::current_exception();}
        {std::lock_guard lock(mutex_);
            if(!retry&&!error){std::lock_guard sourceLock(index_->rejected->mutex);if(index_->rejected->revision==revision){images_[name]=image;touchImage(name);}else retry=true;}
            auto found=pendingImages_.find(name);if(found!=pendingImages_.end()&&found->second==pending)pendingImages_.erase(found);}
        {std::lock_guard lock(pending->mutex);pending->image=retry?std::shared_ptr<Img>{}:image;pending->error=error;pending->done=true;}pending->condition.notify_all();
        if(error)std::rethrow_exception(error);if(retry)continue;return image;
    }
}
bool Assets::frameReady(std::string logical,int index,int palette){
    std::lock_guard lock(mutex_);syncSources();auto name=normalize(std::move(logical));if(index<0)return images_.contains(name);return resolvedFrames_.contains(FrameKey{std::move(name),index,palette});
}
std::shared_ptr<Frame> Assets::frameInner(std::string logical,int index,int palette,bool fallback,const std::function<bool()>& cancelled){
    if(logical.empty())return {};FrameKey key{normalize(std::move(logical)),index,palette};ResourceCancellationScope scope(cancelled);
    for(;;){
        checkResourceCancelled(cancelled);auto revision=sourceRevision();
        {std::lock_guard lock(mutex_);syncSources();if(auto it=resolvedFrames_.find(key);it!=resolvedFrames_.end()){if(it->second.fallback&&!fallback)throw Error("missing resource/frame: "+key.logical);touchImage(it->second.source);return it->second.frame;}}
        ResolvedFrame entry;entry.requested=key.logical;auto decoded=decodeFrame(key.logical,index,palette,&entry.source);
        if(decoded){entry.frame=std::make_shared<Frame>(*decoded);entry.frame->actualPath=key.logical;entry.frame->actualFrame=index;}
        else {if(!fallback)throw Error("missing resource/frame: "+key.logical);auto base=preloadInner("interface/base.img",cancelled);if(!base)throw Error("native missing-resource fallback absent");
            entry.frame=std::make_shared<Frame>(*base->frame(91,0,cancelled));entry.frame->actualPath=normalize("interface/base.img");entry.frame->actualFrame=91;entry.fallback=true;entry.source=entry.frame->actualPath;}
        checkResourceCancelled(cancelled);std::lock_guard lock(mutex_);
        {std::lock_guard sourceLock(index_->rejected->mutex);if(index_->rejected->revision==revision){
            auto [it,inserted]=resolvedFrames_.emplace(key,std::move(entry));touchImage(it->second.source);return it->second.frame;
        }}
        // Never clear sources or cancel the queue while holding its Index lock.
        syncSources();
    }
}
std::shared_ptr<Frame> Assets::frame(std::string logical,int index,int palette,bool fallback) {
    if(logical.empty())return {};FrameKey key{normalize(logical),index,palette};bool ready=frameReady(key.logical,index,palette);
    if(!ready){checkResourceCancelled(demandCancellation_);if(demandWaitCallback_)demandWaitCallback_();}
    if(!ready&&prefetch_){
        std::shared_ptr<Prefetch::Job> job;auto revision=sourceRevision();
        {std::lock_guard lock(prefetch_->mutex);prefetch_->stats.demandMisses++;auto found=prefetch_->jobs.find(key);
            if(found!=prefetch_->jobs.end()&&found->second->epoch==prefetch_->epoch&&found->second->revision==revision){job=found->second;if(job->state==Prefetch::Job::Pending&&job->priority>0){job->priority=0;prefetch_->queues[0].push_back(job);prefetch_->condition.notify_one();}}}
        if(job){auto began=Prefetch::Clock::now();std::unique_lock lock(job->mutex);bool waited=!job->done;
            while(!job->done){checkResourceCancelled(demandCancellation_);job->condition.wait_for(lock,std::chrono::milliseconds(2));}
            auto error=job->error;auto aborted=job->aborted;lock.unlock();checkResourceCancelled(demandCancellation_);
            if(waited){std::lock_guard statsLock(prefetch_->mutex);prefetch_->stats.demandWaits++;prefetch_->stats.demandWaitMilliseconds+=std::chrono::duration<double,std::milli>(Prefetch::Clock::now()-began).count();}
            if(error&&!aborted)std::rethrow_exception(error);
        }
    }
    auto result=frameInner(key.logical,index,palette,fallback,demandCancellation_);
    // A background prune may remove the lookup wrapper after frameInner returns;
    // immutable placement metadata still identifies the native fallback exactly.
    notifyFrame(key.logical,index);if(result->actualPath!=key.logical||result->actualFrame!=index){fallbacks++;notifyFrame(result->actualPath,result->actualFrame);}else decodedFrames++;return result;
}
void Assets::touchImage(const std::string& name){if(images_.contains(name))imageTouches_[name]=++imageClock_;}
size_t Assets::decodedImageBytes()const{
    std::lock_guard lock(mutex_);
    size_t bytes=0;std::unordered_set<const Frame*> frames;std::unordered_set<const Pixels*> pixels;
    auto addPixels=[&](const std::shared_ptr<Pixels>& value){if(value&&pixels.insert(value.get()).second)bytes+=sizeof(Pixels)+value->rgba.capacity();};
    auto addFrame=[&](const std::shared_ptr<Frame>& value){if(value&&frames.insert(value.get()).second){bytes+=sizeof(Frame)+value->actualPath.capacity();addPixels(value->texture);}};
    for(const auto& [name,image]:images_){
        std::lock_guard imageLock(image->mutex_);
        bytes+=sizeof(decltype(images_)::value_type)+sizeof(void*)*6+name.capacity()+sizeof(Img)+image->data_.capacity();
        bytes+=image->records_.capacity()*sizeof(std::vector<int32_t>)+image->atlases_.capacity()*sizeof(std::vector<int32_t>);
        for(const auto& value:image->records_)bytes+=value.capacity()*sizeof(int32_t);
        for(const auto& value:image->atlases_)bytes+=value.capacity()*sizeof(int32_t);
        bytes+=(image->offsets_.capacity()+image->atlasOffsets_.capacity())*sizeof(size_t)+image->palettes_.capacity()*sizeof(Bytes);
        for(const auto& value:image->palettes_)bytes+=value.capacity();
        bytes+=image->frames_.size()*(sizeof(decltype(image->frames_)::value_type)+sizeof(void*)*4);
        bytes+=image->atlasTextures_.size()*(sizeof(decltype(image->atlasTextures_)::value_type)+sizeof(void*)*4);
        for(const auto& [key,value]:image->frames_)addFrame(value);
        for(const auto& [key,value]:image->atlasTextures_)addPixels(value);
    }
    for(const auto& [key,value]:resolvedFrames_){bytes+=sizeof(decltype(resolvedFrames_)::value_type)+sizeof(void*)*4+key.logical.capacity()+value.requested.capacity()+value.source.capacity();addFrame(value.frame);}
    for(const auto& [key,value]:imageTouches_)bytes+=sizeof(decltype(imageTouches_)::value_type)+sizeof(void*)*4+key.capacity();
    return bytes;
}
void Assets::pruneDecodedImages(size_t budgetBytes){
    std::lock_guard lock(mutex_);
    syncSources();if(decodedImageBytes()<=budgetBytes)return;
    // Logical aliases share the IMG's pixel storage. Drop their small lookup
    // wrappers together before evicting complete IMG entries by last use.
    resolvedFrames_.clear();
    struct ObjectCost {size_t owners=0,bytes=0;};
    struct Item {std::string name;uint64_t touch=0;size_t bytes=0;std::vector<const Frame*> frames;std::vector<const Pixels*> pixels;};
    std::unordered_map<const Frame*,ObjectCost> frames;std::unordered_map<const Pixels*,ObjectCost> pixels;
    std::vector<Item> items;items.reserve(images_.size());size_t bytes=0;
    std::vector<std::shared_ptr<Img>> lockedImages;lockedImages.reserve(images_.size());
    std::vector<std::unique_lock<std::recursive_timed_mutex>> imageLocks;imageLocks.reserve(images_.size());
    for(const auto& [name,image]:images_){
        lockedImages.push_back(image);imageLocks.emplace_back(image->mutex_);Item item;item.name=name;auto touch=imageTouches_.find(name);if(touch!=imageTouches_.end())item.touch=touch->second;
        item.bytes=sizeof(decltype(images_)::value_type)+sizeof(void*)*6+name.capacity()+sizeof(Img)+image->data_.capacity();
        item.bytes+=image->records_.capacity()*sizeof(std::vector<int32_t>)+image->atlases_.capacity()*sizeof(std::vector<int32_t>);
        for(const auto& value:image->records_)item.bytes+=value.capacity()*sizeof(int32_t);
        for(const auto& value:image->atlases_)item.bytes+=value.capacity()*sizeof(int32_t);
        item.bytes+=(image->offsets_.capacity()+image->atlasOffsets_.capacity())*sizeof(size_t)+image->palettes_.capacity()*sizeof(Bytes);
        for(const auto& value:image->palettes_)item.bytes+=value.capacity();
        item.bytes+=image->frames_.size()*(sizeof(decltype(image->frames_)::value_type)+sizeof(void*)*4);
        item.bytes+=image->atlasTextures_.size()*(sizeof(decltype(image->atlasTextures_)::value_type)+sizeof(void*)*4);
        if(touch!=imageTouches_.end())item.bytes+=sizeof(decltype(imageTouches_)::value_type)+sizeof(void*)*4+touch->first.capacity();
        std::unordered_set<const Frame*> ownFrames;std::unordered_set<const Pixels*> ownPixels;
        auto addPixels=[&](const std::shared_ptr<Pixels>& value){if(value&&ownPixels.insert(value.get()).second){auto& cost=pixels[value.get()];if(!cost.owners++)cost.bytes=sizeof(Pixels)+value->rgba.capacity();item.pixels.push_back(value.get());}};
        for(const auto& [key,value]:image->frames_)if(value&&ownFrames.insert(value.get()).second){auto& cost=frames[value.get()];if(!cost.owners++)cost.bytes=sizeof(Frame)+value->actualPath.capacity();item.frames.push_back(value.get());addPixels(value->texture);}
        for(const auto& [key,value]:image->atlasTextures_)addPixels(value);
        bytes+=item.bytes;items.push_back(std::move(item));
    }
    for(const auto& [key,cost]:frames)bytes+=cost.bytes;for(const auto& [key,cost]:pixels)bytes+=cost.bytes;
    std::stable_sort(items.begin(),items.end(),[](const Item& a,const Item& b){return a.touch<b.touch;});
    for(const auto& item:items){
        if(bytes<=budgetBytes)break;bytes-=item.bytes;
        for(auto value:item.frames){auto& cost=frames.at(value);if(!--cost.owners)bytes-=cost.bytes;}
        for(auto value:item.pixels){auto& cost=pixels.at(value);if(!--cost.owners)bytes-=cost.bytes;}
        imageTouches_.erase(item.name);images_.erase(item.name);
    }
}
}
