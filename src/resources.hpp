#pragma once
#include "protocol.hpp"
#include <unordered_set>
#include <functional>
#include <atomic>
#include <mutex>

namespace rep {
struct ImagePreparationCancelled : Error {ImagePreparationCancelled():Error("Image preparation cancelled") {}};
struct Pixels {
    int width=0,height=0,logicalWidth=0,logicalHeight=0;
    Bytes rgba;
    // Zero keeps explicit updates unconditional (for example decoded movies).
    // Font atlases advance this version after refreshing their pixel contents.
    uint64_t revision=0;
};
std::shared_ptr<Pixels> nativeImageTexture(std::shared_ptr<Pixels> pixels);
struct Frame {
    std::string actualPath;
    int actualFrame=-1;
    int x=0,y=0,width=0,height=0,fullWidth=0,fullHeight=0;
    float geometryScale=1; // logical REP units per source pixel; 0.5 for a 2x UI alias
    bool empty=false,rotated=false,atlas=false;
    std::array<int,4> rect{};
    std::shared_ptr<Pixels> texture;
    Bytes cropped() const;
};
class Img {
    friend class Assets;
    mutable std::recursive_timed_mutex mutex_;
    Bytes data_;
    std::vector<std::vector<int32_t>> records_,atlases_;
    std::vector<size_t> offsets_,atlasOffsets_;
    std::vector<Bytes> palettes_;
    std::unordered_map<uint64_t,std::shared_ptr<Frame>> frames_;
    std::unordered_map<int,std::shared_ptr<Pixels>> atlasTextures_;
    std::shared_ptr<Pixels> atlas(int index);
    std::shared_ptr<Frame> frameInner(int index,int palette,std::unordered_set<int>& visiting);
public:
    int version=0;
    explicit Img(Bytes data);
    size_t count() const { return records_.size(); }
    void validateFrames();
    void validateFrames(const std::function<bool()>& cancelled);
    std::shared_ptr<Frame> frame(int index,int palette=0,const std::function<bool()>& cancelled={});
};
class Assets {
public:
    struct Index;
    using IndexPtr=std::shared_ptr<const Index>;
    struct FrameRequest {std::string path;int index=0,priority=2,palette=0;};
    struct PrefetchStats {
        uint64_t queued=0,completed=0,cancelled=0,failed=0,demandMisses=0,demandWaits=0;
        double demandWaitMilliseconds=0,jobMilliseconds=0,queueMilliseconds=0,maxJobMilliseconds=0;
        size_t pendingJobs=0,readyFrames=0;
    };
private:
    struct Prefetch;
    struct PendingImage;
    std::unique_ptr<Prefetch> prefetch_;
    mutable std::recursive_mutex mutex_;
    struct Entry {std::string package;uint32_t offset,length;bool patch=false;};
    IndexPtr index_;
    std::filesystem::path root_;
    std::unordered_map<std::string,Entry> entries_;
    std::unordered_map<std::string,std::shared_ptr<Img>> images_;
    std::unordered_map<std::string,std::shared_ptr<PendingImage>> pendingImages_;
    std::unordered_map<std::string,uint64_t> imageTouches_;
    uint64_t imageClock_=0;
    void touchImage(const std::string& name);
    struct FrameKey {
        std::string logical;
        int index,palette;
        bool operator==(const FrameKey&)const=default;
    };
    struct FrameKeyHash {
        size_t operator()(const FrameKey& key)const noexcept {
            auto value=std::hash<std::string>{}(key.logical);
            value^=std::hash<int>{}(key.index)+0x9e3779b9u+(value<<6)+(value>>2);
            return value^(std::hash<int>{}(key.palette)+0x9e3779b9u+(value<<6)+(value>>2));
        }
    };
    struct ResolvedFrame {std::string requested,source;std::shared_ptr<Frame> frame;bool fallback=false;};
    std::unordered_map<FrameKey,ResolvedFrame,FrameKeyHash> resolvedFrames_;
    std::unordered_set<std::string> packages_;
    uint64_t sourceRevision_=0;
    std::function<void(const std::string&,int)> frameObserver_;
    std::function<void()> demandWaitCallback_;
    std::function<bool()> demandCancellation_;
    static IndexPtr buildIndex(std::filesystem::path root,bool patchesEnabled);
    static void indexNative(Index& index);
    static void readPackage(const std::filesystem::path& root,const std::string& name,std::unordered_map<std::string,Entry>& entries,bool patch=false);
    void indexPackage(const std::string& name);
    const Entry* resolve(std::string name);
    bool skipPatch(const std::string& name);
    bool patchSource(const std::string& name);
    void syncSources();
    std::shared_ptr<Frame> decodeFrame(const std::string& name,int index,int palette,std::string* source);
    std::shared_ptr<Frame> frameInner(std::string logical,int index,int palette,bool fallback,const std::function<bool()>& cancelled);
    std::shared_ptr<Img> preloadInner(std::string logical,const std::function<bool()>& cancelled);
    Prefetch& prefetchState();
public:
    uint64_t fallbacks=0,decodedFrames=0;
    std::atomic<uint64_t> patchValidations=0;
    explicit Assets(std::filesystem::path root,bool patchesEnabled=true);
    explicit Assets(IndexPtr index);
    ~Assets();
    IndexPtr index()const{return index_;}
    bool patchesEnabled()const;
    uint64_t sourceRevision()const;
    // install only swaps prepared state. It must not wait or call back into
    // Assets, because the applied Index health decision is held across it.
    bool publishAtRevision(uint64_t expected,const std::function<void()>& install);
    const std::filesystem::path& root()const{return root_;}
    void clearDecodedImages();
    // Estimates owned IMG bytes, frame metadata and unique decoded pixels.
    // External references remain valid when their cache entry is evicted.
    size_t decodedImageBytes()const;
    void pruneDecodedImages(size_t budgetBytes);
    using FrameObserver=std::function<void(const std::string&,int)>;
    FrameObserver frameObserver()const{std::lock_guard lock(mutex_);return frameObserver_;}
    void setFrameObserver(FrameObserver observer){std::lock_guard lock(mutex_);frameObserver_=std::move(observer);}
    void notifyFrame(const std::string& path,int index){auto observer=frameObserver();if(observer)observer(path,index);}
    // Callbacks belong to the render owner. CPU prefetch never invokes them.
    void setDemandWaitCallback(std::function<void()> callback){demandWaitCallback_=std::move(callback);}
    void setDemandCancellation(std::function<bool()> cancelled){demandCancellation_=std::move(cancelled);}
    bool frameReady(std::string logical,int index,int palette=0);
    void prefetch(const std::vector<FrameRequest>& requests,uint64_t generation,const std::function<bool()>& cancelled={});
    void cancelPrefetch();
    std::vector<std::shared_ptr<Frame>> drainReady(uint64_t generation,size_t maxCount=32);
    PrefetchStats prefetchStats()const;
    std::shared_ptr<Img> preload(std::string logical);
    std::shared_ptr<Frame> frame(std::string logical,int index,int palette=0,bool fallback=true);
};
}
