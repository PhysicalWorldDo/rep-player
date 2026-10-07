#pragma once
#include "protocol.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <unordered_set>

namespace rep {
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
    struct Entry {std::filesystem::path package;uint32_t offset=0,length=0;};
    struct Pending {
        std::mutex mutex;
        std::condition_variable ready;
        bool done=false;
        std::shared_ptr<const AudioClip> result;
    };
    std::filesystem::path client_,ffmpeg_;
    std::unordered_map<std::string,SoundDefinition> definitions_;
    std::unordered_map<std::string,Entry> entries_;
    std::unordered_set<std::string> indexedPackages_,unavailable_,reported_;
    std::unordered_map<std::string,std::shared_ptr<const AudioClip>> clips_;
    std::unordered_map<std::string,std::shared_ptr<Pending>> pending_;
    std::vector<std::string> diagnostics_;
    mutable std::mutex mutex_;
    bool allPackagesIndexed_=false;
    std::atomic<uint64_t> decoded_{0},missing_{0};
    void loadDefinitions();
    void indexPackage(const std::filesystem::path& package);
    std::optional<Entry> resolve(const std::string& logical);
    void report(const std::string& key,std::string message,bool missing=false);
public:
    AudioResources(std::filesystem::path clientRoot,std::filesystem::path cacheRoot);
    const SoundDefinition* definition(const std::string& tag)const;
    std::shared_ptr<const AudioClip> clip(const std::string& tag,const std::function<bool()>& cancelled={});
    std::vector<std::string> diagnostics()const;
    uint64_t decoded()const{return decoded_.load();}
    uint64_t missing()const{return missing_.load();}
};
}
