#pragma once
#include "protocol.hpp"
#include <unordered_set>
#include <functional>

namespace rep {
struct Pixels { int width=0,height=0,logicalWidth=0,logicalHeight=0; Bytes rgba; };
std::shared_ptr<Pixels> nativeImageTexture(std::shared_ptr<Pixels> pixels);
struct Frame {
    std::string actualPath;
    int actualFrame=-1;
    int x=0,y=0,width=0,height=0,fullWidth=0,fullHeight=0;
    bool empty=false,rotated=false,atlas=false;
    std::array<int,4> rect{};
    std::shared_ptr<Pixels> texture;
    Bytes cropped() const;
};
class Img {
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
    std::shared_ptr<Frame> frame(int index,int palette=0);
};
class Assets {
public:
    struct Index;
    using IndexPtr=std::shared_ptr<const Index>;
private:
    struct Entry {std::string package;uint32_t offset,length;};
    IndexPtr index_;
    std::filesystem::path root_;
    std::unordered_map<std::string,Entry> entries_;
    std::unordered_map<std::string,std::shared_ptr<Img>> images_;
    std::unordered_set<std::string> packages_;
    std::function<void(const std::string&,int)> frameObserver_;
    static IndexPtr buildIndex(std::filesystem::path root);
    static void indexNative(Index& index);
    static void readPackage(const std::filesystem::path& root,const std::string& name,std::unordered_map<std::string,Entry>& entries);
    void indexPackage(const std::string& name);
    const Entry* resolve(std::string name);
public:
    uint64_t fallbacks=0,decodedFrames=0;
    explicit Assets(std::filesystem::path root);
    explicit Assets(IndexPtr index);
    IndexPtr index()const{return index_;}
    const std::filesystem::path& root()const{return root_;}
    void clearDecodedImages(){images_.clear();}
    using FrameObserver=std::function<void(const std::string&,int)>;
    FrameObserver frameObserver()const{return frameObserver_;}
    void setFrameObserver(FrameObserver observer){frameObserver_=std::move(observer);}
    void notifyFrame(const std::string& path,int index){if(frameObserver_)frameObserver_(path,index);}
    std::shared_ptr<Img> preload(std::string logical);
    std::shared_ptr<Frame> frame(std::string logical,int index,int palette=0,bool fallback=true);
};
}
