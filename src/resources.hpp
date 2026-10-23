#pragma once
#include "protocol.hpp"
#include <unordered_set>
#include <functional>

namespace rep {
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
    std::shared_ptr<Frame> frame(int index,int palette=0);
};
class Assets {
public:
    struct Index;
    using IndexPtr=std::shared_ptr<const Index>;
private:
    struct Entry {std::string package;uint32_t offset,length;bool patch=false;};
    IndexPtr index_;
    std::filesystem::path root_;
    std::unordered_map<std::string,Entry> entries_;
    std::unordered_map<std::string,std::shared_ptr<Img>> images_;
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
    static IndexPtr buildIndex(std::filesystem::path root,bool patchesEnabled);
    static void indexNative(Index& index);
    static void readPackage(const std::filesystem::path& root,const std::string& name,std::unordered_map<std::string,Entry>& entries,bool patch=false);
    void indexPackage(const std::string& name);
    const Entry* resolve(std::string name);
    bool skipPatch(const std::string& name);
    bool patchSource(const std::string& name);
    void syncSources();
    std::shared_ptr<Frame> decodeFrame(const std::string& name,int index,int palette,std::string* source);
public:
    uint64_t fallbacks=0,decodedFrames=0,patchValidations=0;
    explicit Assets(std::filesystem::path root,bool patchesEnabled=true);
    explicit Assets(IndexPtr index);
    IndexPtr index()const{return index_;}
    bool patchesEnabled()const;
    uint64_t sourceRevision()const;
    const std::filesystem::path& root()const{return root_;}
    void clearDecodedImages(){resolvedFrames_.clear();images_.clear();imageTouches_.clear();}
    // Estimates owned IMG bytes, frame metadata and unique decoded pixels.
    // External references remain valid when their cache entry is evicted.
    size_t decodedImageBytes()const;
    void pruneDecodedImages(size_t budgetBytes);
    using FrameObserver=std::function<void(const std::string&,int)>;
    FrameObserver frameObserver()const{return frameObserver_;}
    void setFrameObserver(FrameObserver observer){frameObserver_=std::move(observer);}
    void notifyFrame(const std::string& path,int index){if(frameObserver_)frameObserver_(path,index);}
    std::shared_ptr<Img> preload(std::string logical);
    std::shared_ptr<Frame> frame(std::string logical,int index,int palette=0,bool fallback=true);
};
}
