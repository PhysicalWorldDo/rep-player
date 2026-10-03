#include <cstring>
#include <stdexcept>
#include "protocol.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>

static std::string quote(const std::string& s) {
    std::ostringstream o;o<<'"';for(unsigned char c:s){switch(c){case '"':o<<"\\\"";break;case '\\':o<<"\\\\";break;case '\n':o<<"\\n";break;case '\r':o<<"\\r";break;case '\t':o<<"\\t";break;default:if(c<32)o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;else o<<c;}}o<<'"';return o.str();
}
static std::string hex(std::span<const uint8_t> a){std::ostringstream o;for(auto c:a)o<<std::hex<<std::setw(2)<<std::setfill('0')<<int(c);return o.str();}
static void output(rep::Replay& r,const rep::Statistics& s,bool dump,double ms) {
    std::cout<<"{\"version\":"<<r.version/10.<<",\"minor\":"<<r.header.minor<<",\"width\":"<<r.header.width()<<",\"height\":"<<r.header.height()<<",\"render_mode\":"<<int(r.header.renderMode)
    <<",\"scenes\":"<<s.scenes<<",\"references\":"<<s.references<<",\"aux_bytes\":"<<s.auxBytes<<",\"command_count\":"<<r.dictionary.size()<<",\"resource_migrations\":"<<r.migrations
    <<",\"timeline_crc32\":"<<s.timelineCrc<<",\"exact_eof\":true,\"milliseconds\":"<<ms<<",\"opcode_counts\":{";
    bool first=true;for(int i=0;i<65;i++)if(s.opcodes[i]){if(!first)std::cout<<',';first=false;std::cout<<quote(std::to_string(i))<<':'<<s.opcodes[i];}std::cout<<'}';
    if(dump){
        std::cout<<",\"timestamps\":[";first=true;for(auto t:s.timestamps){if(!first)std::cout<<',';first=false;std::cout<<t;}std::cout<<"],\"resources\":{";
        for(size_t n=0;n<r.resources.size();n++){if(n)std::cout<<',';std::cout<<quote(std::to_string(n))<<':'<<quote(r.resources[n]);}std::cout<<"},\"dictionary\":[";
        std::vector<uint32_t> ids;for(auto& [id,c]:r.dictionary)ids.push_back(id);std::sort(ids.begin(),ids.end());first=true;
        for(auto id:ids){auto& c=r.dictionary.at(id);if(!first)std::cout<<',';first=false;std::cout<<"{\"id\":"<<id<<",\"raw_crc32\":"<<rep::crc(c.raw)<<",\"instructions\":[";
            bool f=true;for(auto& i:c.instructions){if(!f)std::cout<<',';f=false;std::cout<<"{\"opcode\":"<<i.opcode<<",\"payload_bytes\":"<<i.payloadBytes<<",\"aux_bytes\":"<<i.auxBytes;
                if(i.hasParams)std::cout<<",\"params_hex\":"<<quote(hex(i.params));std::cout<<'}';}std::cout<<"]}";}
        std::cout<<']';
    }std::cout<<"}\n";
}
int wmain(int argc,wchar_t** argv){
    try {
        if(argc>2&&std::wstring_view(argv[1])==L"--batch") {
            std::ifstream list{std::filesystem::path(argv[2])};std::string line;int failures=0;
            while(std::getline(list,line)) {if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
                try {auto start=std::chrono::steady_clock::now();rep::Replay r(rep::wide(line));auto s=r.validate();
                    output(r,s,false,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}
                catch(const std::exception& e){std::cout<<"{\"error\":"<<quote(e.what())<<"}\n";++failures;}
            }return failures?1:0;
        }
        bool dump=argc>1&&std::wstring_view(argv[1])==L"--dump";if(argc<(dump?3:2))throw rep::Error("usage: rep_validate [--dump] file.rep");
        auto start=std::chrono::steady_clock::now();rep::Replay r(argv[dump?2:1]);auto s=r.validate(dump);
        output(r,s,dump,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
