#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace rep {
using Bytes = std::vector<uint8_t>;
struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
class Reader {
    std::span<const uint8_t> data_;
public:
    size_t pos = 0;
    explicit Reader(std::span<const uint8_t> data) : data_(data) {}
    size_t remaining() const { return data_.size()-pos; }
    std::span<const uint8_t> take(size_t n);
    template<class T> T get() { auto s=take(sizeof(T)); T v; std::memcpy(&v,s.data(),sizeof(T)); return v; }
    void end() const { if (remaining()) throw Error("unconsumed structure bytes"); }
};
template<class T> T at(std::span<const uint8_t> data,size_t offset) {
    if (offset+sizeof(T)>data.size()) throw Error("truncated native field");
    T v; std::memcpy(&v,data.data()+offset,sizeof(T)); return v;
}
template<class T,class C> void put(C& data,size_t offset,T v) { std::memcpy(data.data()+offset,&v,sizeof(T)); }
uint32_t crc(std::span<const uint8_t> data);
Bytes inflateAll(std::span<const uint8_t> data);
Bytes readFile(const std::filesystem::path& path);
std::string utf8(std::wstring_view text);
std::wstring wide(std::string_view text, unsigned codepage=65001);
std::string canonical(std::string text);

struct Header {
    Bytes raw;
    std::array<uint16_t,8> recorded{};
    std::array<int16_t,8> dimensions{800,600,800,600,800,600,800,600};
    uint16_t minor=0;
    uint8_t country=0,renderMode=0,viewportMode=0,format=8;
    uint32_t buildDate=0,buildSeconds=0;
    int width() const { return dimensions[4]>0?dimensions[4]:800; }
    int height() const { return dimensions[5]>0?dimensions[5]:600; }
};
struct Effect {
    uint32_t type=67;
    bool present=false,obsolete=false;
    int64_t resource=-1;
    std::vector<float> floats;
    std::vector<uint32_t> bits;
    bool programNull() const;
};
struct Instruction {
    uint32_t opcode=0,offset=0,payloadBytes=0,auxBytes=0,storedSize=0;
    std::array<uint8_t,64> params{};
    bool hasParams=false;
    int64_t resource=-1;
    int32_t frame=0;
    uint32_t layer=0;
    Bytes native;
    Effect effect;
    std::vector<std::array<int32_t,4>> instances;
};
struct Command {
    Bytes raw;
    std::vector<Instruction> instructions;
    uint32_t auxBytes=0;
};
struct Scene {
    int32_t timestamp=0;
    uint64_t ordinal=0;
    std::vector<uint32_t> ids;
    Bytes aux;
};
struct Statistics {
    uint64_t scenes=0,references=0,auxBytes=0,emptyBlendPops=0,migrations=0;
    std::array<uint64_t,65> opcodes{},dictionaryOpcodes{};
    std::vector<int32_t> timestamps;
    uint32_t timelineCrc=0;
    bool exactEof=false;
};
class Timeline;
class Replay {
    Bytes compressedTimeline_;
    std::unique_ptr<Timeline> timeline_;
    uint64_t ordinal_=0;
public:
    int version=10;
    Header header;
    std::unordered_map<uint32_t,Command> dictionary;
    std::vector<std::string> rawResources,resources;
    std::vector<Command*> dense;
    uint64_t migrations=0;
    explicit Replay(const std::filesystem::path& path);
    ~Replay();
    Replay(const Replay&)=delete;
    void rewind();
    bool next(Scene& scene);
    const Command* command(uint32_t id) const;
    std::string path(int64_t id) const;
    Statistics validate(bool retainTimestamps=false);
};
std::array<uint8_t,64> drawDefaults();
Command decodeCommand(Bytes raw,int version,int minor);
}
