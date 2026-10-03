#include <cstring>
#include <stdexcept>
#include "protocol.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <limits>

static std::string quote(const std::string& s) {
    std::ostringstream o;o<<'"';for(unsigned char c:s){switch(c){case '"':o<<"\\\"";break;case '\\':o<<"\\\\";break;case '\n':o<<"\\n";break;case '\r':o<<"\\r";break;case '\t':o<<"\\t";break;default:if(c<32)o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;else o<<c;}}o<<'"';return o.str();
}
static std::string hex(std::span<const uint8_t> a){std::ostringstream o;for(auto c:a)o<<std::hex<<std::setw(2)<<std::setfill('0')<<int(c);return o.str();}
static void output(rep::Replay& r,const rep::Statistics& s,bool dump,double ms,const std::filesystem::path& path) {
    std::ostringstream versionBits;versionBits<<"0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<r.versionBits;
    std::cout<<"{\"version\":"<<r.version/10.<<",\"minor\":"<<r.header.minor<<",\"width\":"<<r.header.width()<<",\"height\":"<<r.header.height()<<",\"render_mode\":"<<int(r.header.renderMode)
    <<",\"version_bits\":"<<quote(versionBits.str())<<",\"profile\":"<<quote(rep::profileName(r.options.profile))<<",\"resource_codepage\":"<<r.effectiveCodePage()
    <<",\"resource_strings_decoded\":"<<(r.resourceStringsDecoded()?"true":"false")<<",\"path\":"<<quote(rep::utf8(path.wstring()))
    <<",\"scenes\":"<<s.scenes<<",\"references\":"<<s.references<<",\"aux_bytes\":"<<s.auxBytes<<",\"command_count\":"<<r.dictionary.size()<<",\"resource_migrations\":"<<r.migrations
    <<",\"timeline_crc32\":"<<s.timelineCrc<<",\"exact_eof\":true,\"milliseconds\":"<<ms
    <<",\"compatibility_ignored_commands\":"<<s.compatibilityIgnoredCommands<<",\"compatibility_ignored_references\":"<<s.compatibilityIgnoredReferences<<",\"opcode_counts\":{";
    bool first=true;for(size_t i=0;i<s.opcodes.size();i++)if(s.opcodes[i]){if(!first)std::cout<<',';first=false;std::cout<<quote(std::to_string(i))<<':'<<s.opcodes[i];}std::cout<<'}';
    std::cout<<",\"dictionary_opcode_counts\":{";first=true;for(size_t i=0;i<s.dictionaryOpcodes.size();i++)if(s.dictionaryOpcodes[i]){if(!first)std::cout<<',';first=false;std::cout<<quote(std::to_string(i))<<':'<<s.dictionaryOpcodes[i];}std::cout<<'}';
    if(dump){
        std::cout<<",\"timestamps\":[";first=true;for(auto t:s.timestamps){if(!first)std::cout<<',';first=false;std::cout<<t;}std::cout<<"],\"resources\":{";
        for(size_t n=0;n<r.resources.size();n++){if(n)std::cout<<',';std::cout<<quote(std::to_string(n))<<':'<<quote(r.resources[n]);}
        std::cout<<"},\"resource_bytes_hex\":[";first=true;for(const auto& raw:r.resourceBytes){if(!first)std::cout<<',';first=false;std::cout<<quote(hex(raw));}std::cout<<"],\"dictionary\":[";
        std::vector<uint32_t> ids;for(auto& [id,c]:r.dictionary)ids.push_back(id);std::sort(ids.begin(),ids.end());first=true;
        for(auto id:ids){auto& c=r.dictionary.at(id);if(!first)std::cout<<',';first=false;std::cout<<"{\"id\":"<<id<<",\"raw_crc32\":"<<rep::crc(c.raw)<<",\"instructions\":[";
            bool f=true;for(auto& i:c.instructions){if(!f)std::cout<<',';f=false;std::cout<<"{\"opcode\":"<<i.opcode<<",\"payload_bytes\":"<<i.payloadBytes<<",\"aux_bytes\":"<<i.auxBytes
                <<",\"resource_id\":"<<i.resource<<",\"native_hex\":"<<quote(hex(i.native))<<",\"native_context_state\":"<<(i.nativeContextState?"true":"false")
                <<",\"compatibility_ignored\":"<<(i.compatibilityIgnored?"true":"false");
                if(i.hasParams)std::cout<<",\"params_hex\":"<<quote(hex(i.params));std::cout<<'}';}std::cout<<"]}";}
        std::cout<<']';
    }std::cout<<"}\n";
}
int wmain(int argc,wchar_t** argv){
    try {
        bool dump=false;rep::ReplayOptions options;std::optional<std::filesystem::path> input,batch;
        auto argument=[&](int& index){if(++index>=argc)throw rep::Error("missing option value");return std::wstring_view(argv[index]);};
        for(int index=1;index<argc;++index){
            std::wstring_view arg(argv[index]);
            if(arg==L"--dump")dump=true;
            else if(arg==L"--structural")options.decodeResourceStrings=false;
            else if(arg==L"--profile"){
                auto name=argument(index);
                if(name==L"dfo")options.profile=rep::ProtocolProfile::Dfo;
                else if(name==L"dnf-july")options.profile=rep::ProtocolProfile::DnfJuly2026;
                else if(name==L"dnf-compatible")options.profile=rep::ProtocolProfile::DnfCompatible;
                else throw rep::Error("unsupported protocol profile "+rep::utf8(name));
            }else if(arg==L"--codepage"){
                auto text=std::wstring(argument(index));size_t used=0;auto value=std::stoul(text,&used,10);
                if(used!=text.size()||value>std::numeric_limits<unsigned>::max())throw rep::Error("invalid resource codepage");
                options.resourceCodePage=unsigned(value);
            }else if(arg==L"--batch")batch=std::filesystem::path(argument(index));
            else if(arg.starts_with(L"--"))throw rep::Error("unsupported option "+rep::utf8(arg));
            else {if(input)throw rep::Error("multiple REP paths supplied");input=std::filesystem::path(arg);}
        }
        if(batch){
            if(input)throw rep::Error("batch list and REP path cannot be combined");
            std::ifstream list{*batch};if(!list)throw rep::Error("cannot open REP batch list");std::string line;int failures=0;
            while(std::getline(list,line)) {if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
                try {auto path=std::filesystem::path(rep::wide(line));auto start=std::chrono::steady_clock::now();rep::Replay r(path,options);auto s=r.validate(dump);
                    output(r,s,dump,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),path);}
                catch(const std::exception& e){std::cout<<"{\"path\":"<<quote(line)<<",\"profile\":"<<quote(rep::profileName(options.profile))<<",\"error\":"<<quote(e.what())<<"}\n";++failures;}
            }return failures?1:0;
        }
        if(!input)throw rep::Error("usage: rep_validate [--dump] [--profile dfo|dnf-july|dnf-compatible] [--codepage N] [--structural] file.rep | --batch list.txt");
        auto start=std::chrono::steady_clock::now();rep::Replay r(*input,options);auto s=r.validate(dump);
        output(r,s,dump,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),*input);return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
