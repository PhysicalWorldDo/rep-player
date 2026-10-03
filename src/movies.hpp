#pragma once
#include "resources.hpp"
#include <windows.h>
namespace rep {
struct MovieInfo {int width=0,height=0,frames=0;double rate=0;bool bink=false;};
class Movies {
    struct Instance;
    std::filesystem::path client_,cache_,ffmpeg_;
    std::unordered_map<uint64_t,std::unique_ptr<Instance>> instances_;
    std::unordered_map<std::string,std::filesystem::path> payloads_;
    HMODULE bink_=nullptr;
    std::filesystem::path payload(std::string path);
public:
    uint64_t decoded=0,missing=0;
    Movies(std::filesystem::path client,std::filesystem::path cache);
    ~Movies();
    std::shared_ptr<Frame> frame(std::string path,uint64_t instance,uint32_t timestamp);
    MovieInfo info(uint64_t instance)const;
    uint32_t color(uint64_t instance,uint32_t value,bool modern)const;
    void stop(uint64_t instance);
    void reset();
    std::vector<std::shared_ptr<Pixels>> pixels()const;
    std::shared_ptr<Pixels> pixels(uint64_t instance)const;
};
}
