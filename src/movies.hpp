#pragma once
#include "resources.hpp"
#include <windows.h>
namespace rep {
struct MovieInfo {int width=0,height=0,frames=0;double rate=0;bool bink=false;};
class Movies {
    struct Payload;
    struct Library;
    struct Decoder;
    struct Instance;
    struct State;
    std::filesystem::path client_,cache_,ffmpeg_;
    std::shared_ptr<State> state_,transactionBase_;
    std::unordered_map<std::string,std::weak_ptr<Payload>> payloads_;
    std::unordered_map<std::string,std::weak_ptr<Decoder>> decoders_;
    std::shared_ptr<Library> bink_;
    std::function<void()> demandWait_;
    std::shared_ptr<Payload> payload(std::string path);
    void writable();
public:
    class Snapshot {
        std::shared_ptr<State> state_;
        friend class Movies;
        explicit Snapshot(std::shared_ptr<State> state):state_(std::move(state)){}
    public:
        Snapshot()=default;
    };
    uint64_t decoded=0,missing=0;
    Movies(std::filesystem::path client,std::filesystem::path cache);
    ~Movies();
    void setDemandWaitCallback(std::function<void()> callback){demandWait_=std::move(callback);}
    // Bink: recorded one-based frame number (0 aliases 1). AVI: milliseconds.
    std::shared_ptr<Frame> frame(std::string path,uint64_t instance,uint32_t position);
    MovieInfo info(uint64_t instance)const;
    uint32_t color(uint64_t instance,uint32_t value,bool modern)const;
    void stop(uint64_t instance);
    void reset();
    Snapshot snapshot()const;
    void restoreSnapshot(const Snapshot& snapshot);
    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction();
    std::vector<std::shared_ptr<Pixels>> pixels()const;
    std::shared_ptr<Pixels> pixels(uint64_t instance)const;
};
}
