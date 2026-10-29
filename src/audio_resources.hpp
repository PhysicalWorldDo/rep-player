#pragma once
#include "protocol.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <unordered_set>

namespace rep {
class RetainedCache;
struct AudioClip {
    int sampleRate=48000,channels=2;
    std::vector<float> samples;
    uint64_t frames()const{return channels>0?samples.size()/size_t(channels):0;}
};
enum class AudioKind {Voice,Effect,Music,Ambient,UninterruptedEffect,Composite,Unknown,Random,Group,RandomGroup};
struct SoundChild {std::string tag;double delayMs=0,delayRangeMs=0;};
struct SoundDefinition {
    std::string tag,file;
    AudioKind kind=AudioKind::Unknown;
    bool playable=false,nativeFiltered=false;
    // XML delays are seconds; native sources store integer milliseconds truncated toward zero.
    double loopDelay=-1,loopDelayRange=0;
    int loopCount=0,duplicateLimit=0;
    double volumeAdjust=0,volumeAdjustRange=0;
    std::string duplicatePolicy,fadeData,sidechainData;
    std::vector<SoundChild> children;
    bool ignore3dSound=false;
    float gain=1;
};
class AudioResources {
public:
    struct Index;
    using IndexPtr=std::shared_ptr<const Index>;
    static IndexPtr buildIndex(std::filesystem::path clientRoot,bool patchesEnabled=true);
private:
    struct Entry {std::filesystem::path package;uint32_t offset=0,length=0;bool patch=false;};
    struct Pending {
        std::mutex mutex;
        std::condition_variable ready;
        bool done=false;
        bool cancelled=false;
        std::shared_ptr<const AudioClip> result;
    };
    IndexPtr index_;
    struct Shared;
    struct PackageTable;
    std::shared_ptr<Shared> shared_;
    std::shared_ptr<RetainedCache> cache_;
    std::filesystem::path ffmpeg_;
    std::filesystem::path workRoot_;
    std::unordered_set<std::string> reported_;
    std::vector<std::string> diagnostics_;
    mutable std::mutex mutex_;
    std::atomic<uint64_t> decoded_{0},missing_{0},decoderProcesses_{0};
    static void loadDefinitions(Index& index);
    static void readPackage(const std::filesystem::path& package,std::unordered_map<std::string,Entry>& entries,bool patch=false);
    std::shared_ptr<const PackageTable> indexPackage(const std::filesystem::path& package);
    std::optional<Entry> resolve(const std::string& logical,bool original=false);
    void report(const std::string& key,std::string message,bool missing=false);
public:
    AudioResources(std::filesystem::path clientRoot,std::filesystem::path cacheRoot,bool patchesEnabled=true,std::shared_ptr<RetainedCache> cache={});
    explicit AudioResources(IndexPtr index,std::shared_ptr<RetainedCache> cache={},std::filesystem::path workRoot={});
    struct ClipState {bool ready=false;std::shared_ptr<const AudioClip> clip;};
    // Cache-only acquisition: never reads packages, launches a decoder, or waits.
    ClipState prepared(const std::string& tag);
    void trimCache();
    std::vector<std::shared_ptr<const AudioClip>> prepareClips(const std::vector<std::string>& tags,const std::function<bool()>& cancelled={});
    const SoundDefinition* definition(const std::string& tag)const;
    std::shared_ptr<const AudioClip> clip(const std::string& tag,const std::function<bool()>& cancelled={});
    std::vector<std::string> diagnostics()const;
    uint64_t decoded()const{return decoded_.load();}
    uint64_t missing()const{return missing_.load();}
    uint64_t decoderProcesses()const{return decoderProcesses_.load();}
};
}
