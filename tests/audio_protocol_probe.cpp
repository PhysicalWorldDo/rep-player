#include "protocol.hpp"
#include <iostream>
#include <set>

static std::string hex(std::span<const uint8_t> bytes) {
    constexpr char digits[]="0123456789abcdef";std::string out;
    for(auto byte:bytes){out+=digits[byte>>4];out+=digits[byte&15];}return out;
}
static rep::ProtocolProfile profile(std::string_view value) {
    if(value=="dfo")return rep::ProtocolProfile::Dfo;
    if(value=="dnf-july")return rep::ProtocolProfile::DnfJuly2026;
    if(value=="dnf-compatible")return rep::ProtocolProfile::DnfCompatible;
    throw rep::Error("unknown probe profile");
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc==4&&std::wstring_view(argv[1])==L"--replay"){
            rep::ReplayOptions options;options.profile=profile(rep::utf8(argv[2]));options.resourceCodePage=936;
            rep::Replay replay(argv[3],options);auto stats=replay.validate();std::set<std::string> tags;
            rep::Scene scene;replay.rewind();int last=0;
            while(replay.next(scene)){last=scene.timestamp;for(auto id:scene.ids)if(auto command=replay.command(id))
                for(const auto& i:command->instructions)if(i.opcode==6)tags.insert(replay.path(i.resource));}
            std::cout<<"{\"scenes\":"<<stats.scenes<<",\"last_ms\":"<<last<<",\"op6\":"<<stats.opcodes[6]
                     <<",\"op7\":"<<stats.opcodes[7]<<",\"op41\":"<<stats.opcodes[41]<<",\"tags\":"<<tags.size()
                     <<",\"music\":"<<(tags.contains("M_ARADPVP_PUB")?"true":"false")
                     <<",\"ambient\":"<<(tags.contains("AMB_DARKSTAGE_01")?"true":"false")
                     <<",\"timeline_crc\":"<<stats.timelineCrc<<",\"exact_eof\":"<<(stats.exactEof?"true":"false")<<"}\n";return 0;
        }
        if(argc!=5)throw rep::Error("usage: audio_protocol_probe PROFILE VERSION MINOR HEX");
        auto encoded=rep::utf8(argv[4]);if(encoded.size()%2)throw rep::Error("odd probe hex length");rep::Bytes raw;
        for(size_t j=0;j<encoded.size();j+=2)raw.push_back(uint8_t(std::stoul(encoded.substr(j,2),nullptr,16)));
        auto command=rep::decodeCommand(std::move(raw),std::stoi(argv[2]),std::stoi(argv[3]),profile(rep::utf8(argv[1])));
        std::cout<<"{\"aux_bytes\":"<<command.auxBytes<<",\"raw\":\""<<hex(command.raw)<<"\",\"instructions\":[";bool first=true;
        for(const auto& i:command.instructions){if(!first)std::cout<<',';first=false;
            std::cout<<"{\"opcode\":"<<i.opcode<<",\"resource\":"<<i.resource<<",\"stored_size\":"<<i.storedSize
                     <<",\"payload_bytes\":"<<i.payloadBytes<<",\"aux_bytes\":"<<i.auxBytes
                     <<",\"native\":\""<<hex(i.native)<<"\"}";}
        std::cout<<"]}\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
