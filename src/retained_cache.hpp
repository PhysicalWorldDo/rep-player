#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace rep {
// One controller-owned store for immutable parsed data, PCM and font results.
// The budget covers estimated payload bytes held only by this cache. Objects
// pinned by a live session are reported separately, not silently discarded.
// Insert is O(1); explicit trim maintenance points enforce the inactive budget.
// Sessions call trim after release and periodically/between preparation batches,
// never once per glyph/clip. Peak usage between those points is not a hard cap.
class RetainedCache {
public:
    struct Stats {
        size_t retainedBytes=0,inactiveBytes=0,activeBytes=0,entries=0,evictions=0,budget=0;
        uint64_t hits=0,misses=0;
    };
private:
    struct Entry {
        std::shared_ptr<const void> value;
        std::type_index type;
        size_t bytes;
        uint64_t used;
    };
    struct Owner {
        size_t bytes=0,aliases=0;
        uint64_t used=0;
        std::vector<std::string> keys;
    };
    using Owners=std::map<std::weak_ptr<const void>,Owner,std::owner_less<std::weak_ptr<const void>>>;
    mutable std::mutex mutex_;
    std::unordered_map<std::string,Entry> entries_;
    size_t budget_,evictions_=0;
    uint64_t clock_=0,hits_=0,misses_=0;
    Owners ownersLocked() const {
        Owners owners;
        for(const auto& [key,entry]:entries_){
            auto& owner=owners[entry.value];owner.bytes=(std::max)(owner.bytes,entry.bytes);
            owner.used=(std::max)(owner.used,entry.used);++owner.aliases;owner.keys.push_back(key);
        }
        return owners;
    }
    void trimLocked(std::vector<std::shared_ptr<const void>>& released) {
        auto owners=ownersLocked();size_t inactive=0;
        using Item=typename Owners::value_type;
        std::vector<const Item*> victims;
        for(const auto& item:owners)if(item.first.use_count()<=long(item.second.aliases)){
            inactive+=item.second.bytes;victims.push_back(&item);
        }
        std::sort(victims.begin(),victims.end(),[](auto a,auto b){return a->second.used<b->second.used;});
        for(auto victim:victims){
            if(inactive<=budget_)break;
            // A weak lookup outside this store may have pinned it since the
            // first accounting pass. shared_ptr still protects any later race.
            if(victim->first.use_count()>long(victim->second.aliases))continue;
            for(const auto& key:victim->second.keys){auto it=entries_.find(key);if(it!=entries_.end()){released.push_back(std::move(it->second.value));entries_.erase(it);++evictions_;}}
            inactive-=victim->second.bytes;
        }
    }
public:
    explicit RetainedCache(size_t budget=256*1024*1024):budget_(budget){}
    template<class T> std::shared_ptr<const T> find(const std::string& key) {
        std::lock_guard lock(mutex_);auto it=entries_.find(key);
        if(it==entries_.end()||it->second.type!=std::type_index(typeid(T))){++misses_;return {};}
        ++hits_;it->second.used=++clock_;return std::static_pointer_cast<const T>(it->second.value);
    }
    template<class T> void insert(std::string key,std::shared_ptr<const T> value,size_t estimatedBytes) {
        if(!value)return;
        std::vector<std::shared_ptr<const void>> released;
        {std::lock_guard lock(mutex_);auto it=entries_.find(key);
            if(it!=entries_.end()){released.push_back(std::move(it->second.value));entries_.erase(it);}
            entries_.emplace(std::move(key),Entry{std::move(value),std::type_index(typeid(T)),estimatedBytes,++clock_});
        } // Large payload destructors run after unlocking the shared store.
    }
    void trim(){std::vector<std::shared_ptr<const void>> released;{std::lock_guard lock(mutex_);trimLocked(released);}}
    void trim(size_t budget){std::vector<std::shared_ptr<const void>> released;{std::lock_guard lock(mutex_);budget_=budget;trimLocked(released);}}
    void clear(){decltype(entries_) released;{std::lock_guard lock(mutex_);released.swap(entries_);}}
    void erasePrefix(const std::string& prefix){
        std::vector<std::shared_ptr<const void>> released;
        {std::lock_guard lock(mutex_);for(auto it=entries_.begin();it!=entries_.end();)if(it->first.starts_with(prefix)){released.push_back(std::move(it->second.value));it=entries_.erase(it);}else ++it;}
    }
    Stats stats() const {
        std::lock_guard lock(mutex_);Stats result;result.entries=entries_.size();result.evictions=evictions_;result.budget=budget_;result.hits=hits_;result.misses=misses_;
        for(const auto& [weak,owner]:ownersLocked()){
            result.retainedBytes+=owner.bytes;
            if(weak.use_count()>long(owner.aliases))result.activeBytes+=owner.bytes;else result.inactiveBytes+=owner.bytes;
        }
        return result;
    }
};
}
