#include <cstring>
#include <stdexcept>
#include "protocol.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <chrono>
#include <windows.h>
#include <zlib.h>
#include "migration_table.hpp"

namespace rep {
std::span<const uint8_t> Reader::take(size_t n) {
    if(n>remaining()) throw Error("truncated structure at byte "+std::to_string(pos));
    auto s=data_.subspan(pos,n); pos+=n; return s;
}
uint32_t crc(std::span<const uint8_t> d) { return uint32_t(crc32(0,d.data(),uInt(d.size()))); }
Bytes readFile(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary|std::ios::ate);
    if(!f) throw Error("cannot open "+utf8(p.wstring()));
    auto n=f.tellg(); if(n<0) throw Error("cannot obtain file length");
    Bytes b(static_cast<size_t>(n)); f.seekg(0); f.read(reinterpret_cast<char*>(b.data()),b.size());
    if(!f) throw Error("truncated file read"); return b;
}
std::string utf8(std::wstring_view t) {
    if(t.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,t.data(),int(t.size()),nullptr,0,nullptr,nullptr);
    std::string s(n,'\0'); WideCharToMultiByte(CP_UTF8,0,t.data(),int(t.size()),s.data(),n,nullptr,nullptr); return s;
}
std::wstring wide(std::string_view t,unsigned cp) {
    if(t.empty()) return {};
    int n=MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,t.data(),int(t.size()),nullptr,0);
    if(!n) throw Error("invalid resource string encoding");
    std::wstring s(n,L'\0'); MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,t.data(),int(t.size()),s.data(),n); return s;
}
std::string canonical(std::string s) {
    for(auto& c:s) { if(c=='\\') c='/'; else if(c>='A'&&c<='Z') c+=32; } return s;
}
class Timeline {
    z_stream z_{};
    std::array<uint8_t,131072> buffer_{};
    size_t begin_=0,end_=0;
    bool ended_=false;
public:
    uint32_t checksum=0;
    explicit Timeline(std::span<const uint8_t> in) {
        if(in.size()>UINT_MAX) throw Error("compressed stream exceeds zlib input bound");
        z_.next_in=const_cast<Bytef*>(in.data()); z_.avail_in=uInt(in.size());
        if(inflateInit(&z_)!=Z_OK) throw Error("inflate initialization failed");
    }
    ~Timeline() { inflateEnd(&z_); }
    bool available() {
        if(begin_<end_) return true;
        if(ended_) return false;
        z_.next_out=buffer_.data(); z_.avail_out=uInt(buffer_.size());
        int r=inflate(&z_,Z_NO_FLUSH); begin_=0; end_=buffer_.size()-z_.avail_out;
        if(r==Z_STREAM_END) { ended_=true; if(z_.avail_in) throw Error("trailing bytes after zlib stream"); }
        else if(r!=Z_OK) throw Error("invalid or truncated zlib stream");
        if(!end_&&!ended_) throw Error("zlib stream makes no progress");
        return begin_<end_;
    }
    void read(void* out,size_t n) {
        auto dest=static_cast<uint8_t*>(out);
        while(n) {
            if(!available()) throw Error("truncated timeline record");
            auto k=std::min(n,end_-begin_);
            std::memcpy(dest,buffer_.data()+begin_,k);
            checksum=uint32_t(crc32(checksum,buffer_.data()+begin_,uInt(k)));
            begin_+=k; dest+=k; n-=k;
        }
    }
    template<class T> T get() { T v; read(&v,sizeof(v)); return v; }
};
Bytes inflateAll(std::span<const uint8_t> in) {
    Bytes out;
    z_stream z{}; z.next_in=const_cast<Bytef*>(in.data()); z.avail_in=uInt(in.size());
    if(inflateInit(&z)!=Z_OK) throw Error("inflate initialization failed");
    struct Guard {z_stream* z; ~Guard(){inflateEnd(z);}} guard{&z};
    std::array<uint8_t,131072> b{};
    int r;
    do {
        z.next_out=b.data(); z.avail_out=b.size(); r=inflate(&z,Z_NO_FLUSH);
        if(r!=Z_OK&&r!=Z_STREAM_END) throw Error("invalid dictionary zlib stream");
        out.insert(out.end(),b.begin(),b.begin()+b.size()-z.avail_out);
    } while(r!=Z_STREAM_END);
    if(z.avail_in) throw Error("trailing dictionary zlib bytes"); return out;
}
bool Effect::programNull() const {
    if(!present||obsolete) return false;
    switch(type) {case 8:case 9:case 18:case 19:case 22:case 43:case 45:case 62:case 63:return true;}
    return type>66;
}
std::array<uint8_t,64> drawDefaults() {
    std::array<uint8_t,64> a{};
    put(a,8,0xffffffffu); put(a,20,1.f); put(a,24,1.f);
    put(a,28,0x4f800000u); put(a,32,0x4f800000u); put(a,52,1.f); put(a,56,1.f); return a;
}
static Bytes padded(Reader& r,uint32_t size,Bytes defaults) {
    if(size>defaults.size()) throw Error("native payload exceeds ABI size");
    auto b=r.take(size); std::copy(b.begin(),b.end(),defaults.begin()); return defaults;
}
Command decodeCommand(Bytes raw,int v,int minor) {
    Command c; c.raw=std::move(raw); Reader r(c.raw);
    while(r.remaining()) {
        Instruction i; i.opcode=r.get<uint32_t>(); i.offset=uint32_t(r.pos);
        auto op=i.opcode;
        if(op>64) throw Error("unsupported opcode "+std::to_string(op));
        switch(op) {
        case 0: {auto t=r.get<uint32_t>(); r.take(4); if((t&255)==7) r.take(8); break;}
        case 1:case 8:case 9:case 11:case 12:case 17:case 20:case 22:case 25:
        case 31:case 34:case 35:case 36:case 37:case 38:case 39:case 40:case 41:
        case 49:case 54:case 59:case 62:break;
        case 2:r.take(8);break;
        case 3:case 4: {
            i.storedSize=r.get<uint32_t>(); if(i.storedSize>64) throw Error("draw payload exceeds ABI");
            i.params=drawDefaults(); auto b=r.take(i.storedSize); std::copy(b.begin(),b.end(),i.params.begin());
            i.hasParams=true; i.auxBytes=4; i.resource=at<uint32_t>(i.params,0); i.frame=at<int16_t>(i.params,4);break;
        }
        case 5: {
            if(v<15||minor<5) {auto b=r.take(16); i.native.assign(b.begin(),b.end()); i.resource=at<uint32_t>(b,12);}
            else {i.storedSize=r.get<uint32_t>(); Bytes d(20); d[6]=1; put(d,8,0xffffffffu); put(d,12,0xffffffffu);
                i.native=padded(r,i.storedSize,std::move(d)); i.resource=at<uint32_t>(i.native,0);}
            i.auxBytes=4;break;
        }
        case 6: {auto s=r.get<uint32_t>(); if(s>36) throw Error("audio payload exceeds ABI"); r.take(s);break;}
        case 7:r.take(20);break;
        case 10: {
            auto count=r.get<uint32_t>(); auto p=r.take(64); std::copy(p.begin(),p.end(),i.params.begin()); i.hasParams=true;
            i.resource=at<uint32_t>(i.params,0); i.auxBytes=4;
            if(count>r.remaining()/10) throw Error("truncated repeated draw");
            while(count--) {int32_t length=int32_t(r.get<uint32_t>()); int32_t f=r.get<int16_t>(),x=r.get<int16_t>(),y=r.get<int16_t>(); i.instances.push_back({length,f,x,y});}break;
        }
        case 13:r.take(64);break;
        case 14:r.take(36);break;
        case 15:if(r.get<uint8_t>()) {auto b=r.take(36);i.resource=at<uint32_t>(b,0);i.frame=at<int32_t>(b,4);}break;
        case 16:r.take(1);break;
        case 18:r.take(6);break;
        case 19: {
            auto& e=i.effect; e.present=r.get<uint8_t>()!=0;
            if(v<13) {auto b=r.take(120);e.type=at<uint32_t>(b,0);e.obsolete=true;}
            else if(e.present) {
                auto t=r.get<uint32_t>(),nf=r.get<uint32_t>(); e.type=t; uint32_t nb=0,x=0,y=0;
                if(v==13) {auto res=r.get<uint32_t>(); nb=r.get<uint32_t>(); x=r.get<uint32_t>();y=r.get<uint32_t>(); if(r.get<uint32_t>()&255)e.resource=res;}
                else if(minor) {bool has=r.get<uint8_t>()!=0;auto res=r.get<uint32_t>();nb=r.get<uint32_t>();if(has)e.resource=res;}
                else {auto has=r.get<uint32_t>();auto res=r.get<uint32_t>();if(has&255)e.resource=res;}
                if(nf>r.remaining()/4) throw Error("truncated shader float vector");
                while(nf--) e.floats.push_back(r.get<float>());
                if(nb>r.remaining()/4) throw Error("truncated shader bit vector");
                while(nb--) {auto bits=r.get<uint32_t>(); if(v==13){float f;std::memcpy(&f,&bits,4);e.floats.push_back(f);}else e.bits.push_back(bits);}
                if(v==13){e.floats.push_back(float(x));e.floats.push_back(float(y));} i.resource=e.resource;
            }break;
        }
        case 21: {
            bool old=v<16&&minor!=6; r.take(old?76:80); auto b=r.take(68); std::copy(b.begin(),b.begin()+64,i.params.begin());
            i.hasParams=true;i.auxBytes=4;i.storedSize=68;break;
        }
        case 23: {auto b=r.take(56);i.resource=at<uint32_t>(b,0);i.layer=at<uint32_t>(b,48);break;}
        case 24:r.take(16);break;
        case 26:case 27:case 29:case 63:r.take(1);break;
        case 28:r.take(3);break;
        case 30: {Bytes d(32);put(d,12,1u);put(d,16,2u);i.storedSize=r.get<uint32_t>();i.native=padded(r,i.storedSize,std::move(d));break;}
        case 32: {r.take(4);r.take(size_t(r.get<uint8_t>())*2);r.take(size_t(r.get<uint8_t>())*2);i.resource=r.get<uint32_t>();i.frame=r.get<int32_t>();break;}
        case 33:i.resource=r.get<uint32_t>();i.frame=r.get<int32_t>();break;
        case 42:case 43:case 44:case 45:case 46:case 47: {
            int m=(op-42)%3; int ls[]={48,60,76},ss[]={40,52,64};Bytes d(ls[m]);
            if(m<2){put(d,16,1.f);put(d,20,1.f);put(d,28,0xffffffffu);put(d,32,9.9999997e37f);put(d,36,9.9999997e37f);}
            else {put(d,60,0xffffffffu);put(d,64,1.f);put(d,68,1.f);}
            if(minor<7) {i.resource=r.get<uint32_t>();auto b=r.take(ls[m]);d.assign(b.begin(),b.end());i.storedSize=ls[m];}
            else {
                i.storedSize=r.get<uint32_t>();i.resource=r.get<uint32_t>();Bytes s(ss[m]);
                if(m<2) {put(s,16,1.f);put(s,20,1.f);put(s,28,0xffffffffu);put(s,32,9.9999997e37f);put(s,36,9.9999997e37f);if(m==1){put(s,44,1.f);put(s,48,1.f);}}
                else {put(s,52,0xffffffffu);put(s,56,1.f);put(s,60,1.f);}
                s=padded(r,i.storedSize,std::move(s));
                if(m<2) {int dst[]={0,4,16,20,24,28,32,36},src[]={0,8,16,20,24,28,32,36};for(int n=0;n<8;n++)std::memcpy(d.data()+dst[n],s.data()+src[n],4);
                    d[40]=s[4];d[41]=s[12];d[42]=s[13];if(m==1)std::memcpy(d.data()+44,s.data()+40,12);}
                else {std::memcpy(d.data(),s.data(),8);d[72]=s[8];d[16]=s[9];d[36]=s[28];std::memcpy(d.data()+20,s.data()+12,16);
                    std::memcpy(d.data()+40,s.data()+32,16);std::memcpy(d.data()+56,s.data()+48,16);}
            }
            i.native=std::move(d);i.frame=at<int32_t>(i.native,0);i.layer=at<uint32_t>(i.native,4);
            i.params=drawDefaults();put(i.params,0,uint32_t(i.resource));put(i.params,4,uint16_t(i.frame));i.params[6]=i.native[m<2?40:72];
            put(i.params,8,at<uint32_t>(i.native,m<2?28:60));
            if(m<2) {put(i.params,16,at<float>(i.native,24));std::memcpy(i.params.data()+20,i.native.data()+16,8);std::memcpy(i.params.data()+28,i.native.data()+32,8);}
            else std::memcpy(i.params.data()+20,i.native.data()+64,8);
            i.hasParams=true;i.auxBytes=4;break;
        }
        case 48: {
            i.resource=r.get<uint32_t>();auto b=r.take(44);i.native.assign(b.begin(),b.end());i.layer=r.get<uint32_t>();
            if(minor<7) {b=r.take(8);i.native.insert(i.native.end(),b.begin(),b.end());}else i.auxBytes=4;
            i.native.push_back(r.get<uint8_t>());break;
        }
        case 50:r.take(24);break;
        case 51:case 52:r.take(2);break;
        case 53:r.take(6);break;
        case 55:case 56:r.take(8);break;
        case 57: {
            i.resource=r.get<uint32_t>();auto b=r.take(v<18?32:40);i.native.assign(b.begin(),b.end());
            i.native.resize(40);if(v<18){put(i.native,32,1.f);put(i.native,36,1.f);}
            i.frame=at<int32_t>(i.native,0);i.layer=at<uint32_t>(i.native,4);
            auto nc=r.get<uint64_t>(),nr=r.get<uint64_t>();if(nc>r.remaining()/4||nr>r.remaining()/4-nc)throw Error("truncated shader grid");
            r.take(size_t(nc+nr)*4);r.take(8);break;
        }
        case 58: {i.resource=r.get<uint32_t>();auto b=r.take(36);i.frame=at<int32_t>(b,0);break;}
        case 60:r.take(16);break;
        case 61:r.take(32);break;
        case 64:r.take(12);break;
        default:throw Error("unimplemented opcode");
        }
        i.payloadBytes=uint32_t(r.pos-i.offset);c.auxBytes+=i.auxBytes;c.instructions.push_back(std::move(i));
    }
    return c;
}
static Header decodeHeader(std::span<const uint8_t> data,int v) {
    Header h;h.raw.assign(data.begin(),data.end());Reader r(data);
    while(r.remaining()) {
        auto tag=r.get<uint8_t>();
        switch(tag) {
        case 0:case 1:case 5:break;
        case 2: {auto b=r.take(16);std::memcpy(h.recorded.data(),b.data(),16);break;}
        case 3:h.format=r.get<uint8_t>();break;
        case 4:case 6:case 7:case 8:case 10:r.take(4);break;
        case 9:{auto n=r.get<uint32_t>();if(n>32)throw Error("too many header actors");r.take(n*(v<11?88:176));break;}
        case 11:{if(v<12)throw Error("viewport tag in old REP");h.viewportMode=r.get<uint8_t>();auto b=r.take(16);std::memcpy(h.dimensions.data(),b.data(),16);break;}
        case 12:{if(v<14)throw Error("extension tag in old REP");auto b=r.take(128);h.minor=at<uint16_t>(b,0);h.country=b[2];h.buildDate=at<uint32_t>(b,4);h.buildSeconds=at<uint32_t>(b,8);break;}
        case 13:if(v<15)throw Error("render mode tag in old REP");h.renderMode=r.get<uint8_t>();break;
        default:throw Error("unsupported header tag "+std::to_string(tag));
        }
    }return h;
}
static int64_t timeValue(int y,int m,int d,int seconds,int millis=0) {
    using namespace std::chrono;
    year_month_day date{year{y},month{unsigned(m)},day{unsigned(d)}};
    if(!date.ok())return 0;
    return duration_cast<milliseconds>(sys_days{date}.time_since_epoch()).count()+int64_t(seconds)*1000+millis;
}
static void migrate(Replay& r) {
    const auto& t=r.header.recorded;
    auto recorded=timeValue(t[0],t[1],t[3],t[4]*3600+t[5]*60+t[6],t[7]);if(!recorded)return;
    auto date=r.header.buildDate;auto built=timeValue(date/10000,date/100%100,date%100,r.header.buildSeconds);
    bool legacy=r.version<=13;if(!legacy&&!built)return;
    for(size_t id=0;id<r.resources.size();++id) {
        auto key=canonical(r.resources[id]);int64_t best=INT64_MAX;const Migration* chosen=nullptr;
        for(const auto& rule:migrationTable) {
            if(rule.legacy!=legacy||key!=rule.source)continue;
            auto cutoff=timeValue(rule.date/10000,rule.date/100%100,rule.date%100,rule.seconds);
            auto force=rule.forceDate?timeValue(rule.forceDate/10000,rule.forceDate/100%100,rule.forceDate%100,rule.forceSeconds):0;
            bool ok=legacy?recorded<cutoff:(rule.country==r.header.country&&built<cutoff&&(!force||recorded<force));
            if(ok&&cutoff<best){best=cutoff;chosen=&rule;}
        }
        if(chosen){r.resources[id]=chosen->target;++r.migrations;}
    }
}
Replay::Replay(const std::filesystem::path& path) {
    auto data=readFile(path);Reader r(data);auto checksum=r.get<uint32_t>(),size=r.get<uint32_t>();
    if(size!=data.size()-8)throw Error("invalid REP declared file length");
    if(checksum!=crc(std::span(data).subspan(8)))throw Error("invalid REP payload CRC32");
    if(r.get<uint8_t>()!=1)throw Error("invalid REP version tag");
    auto value=r.get<float>();version=int(std::round(value*10));
    if(version<10||version>17||std::abs(value-version/10.f)>.00001f)throw Error("unsupported REP version");
    if(r.get<uint8_t>()!=0)throw Error("invalid REP header tag");
    header=decodeHeader(r.take(r.get<uint32_t>()),version);
    auto b=r.take(r.get<uint32_t>());compressedTimeline_.assign(b.begin(),b.end());
    auto dict=inflateAll(r.take(r.get<uint32_t>()));r.end();Reader d(dict);
    Bytes previous;int64_t largest=-1;auto count=d.get<uint32_t>();
    while(count--) {
        auto id=d.get<uint32_t>();auto n=version>=15?d.get<uint16_t>():d.get<uint32_t>();auto s=d.take(n);Bytes raw(s.begin(),s.end());
        if(dictionary.contains(id))throw Error("duplicate dictionary ID");
        if(n>largest)largest=n;else {if(previous.size()<n)throw Error("short XOR predecessor");for(size_t k=0;k<n;k++)raw[k]^=previous[k];}
        previous=raw;dictionary.emplace(id,decodeCommand(std::move(raw),version,header.minor));
    }
    count=d.get<uint32_t>();resources.reserve(count);
    while(count--) {
        if(version>=15&&!d.get<uint8_t>()){resources.emplace_back();continue;}
        auto n=version>=15?d.get<uint16_t>():d.get<uint32_t>();auto s=d.take(size_t(n)+1);
        if(s.back())throw Error("unterminated resource string");
        auto end=std::find(s.begin(),s.end(),0);auto text=std::string_view(reinterpret_cast<const char*>(s.data()),end-s.begin());
        resources.push_back(utf8(wide(text,949)));
    }
    d.end();rawResources=resources;migrate(*this);
    uint32_t max=0;for(auto& [id,c]:dictionary)max=std::max(max,id);
    if(max<1000000){dense.resize(size_t(max)+1);for(auto& [id,c]:dictionary)dense[id]=&c;}
    rewind();
}
Replay::~Replay()=default;
void Replay::rewind(){timeline_=std::make_unique<Timeline>(compressedTimeline_);ordinal_=0;}
const Command* Replay::command(uint32_t id) const {
    if(!dense.empty())return id<dense.size()?dense[id]:nullptr;
    auto it=dictionary.find(id);return it==dictionary.end()?nullptr:&it->second;
}
std::string Replay::path(int64_t id) const {return id>=0&&size_t(id)<resources.size()?resources[id]:std::string{};}
bool Replay::next(Scene& s) {
    if(!timeline_->available())return false;
    s.timestamp=timeline_->get<int32_t>();s.ordinal=ordinal_++;auto n=timeline_->get<uint32_t>();
    if(n%4)throw Error("non-DWORD scene ID array");s.ids.resize(n/4);timeline_->read(s.ids.data(),n);
    n=timeline_->get<uint32_t>();s.aux.resize(n);timeline_->read(s.aux.data(),n);
    uint64_t expected=0;for(auto id:s.ids)if(auto c=command(id))expected+=c->auxBytes;
    if(expected!=n)throw Error("scene auxiliary stream length mismatch at scene "+std::to_string(s.ordinal));
    return true;
}
Statistics Replay::validate(bool keep) {
    rewind();Statistics st;st.migrations=migrations;
    for(auto& [id,c]:dictionary)for(auto& i:c.instructions)st.dictionaryOpcodes[i.opcode]++;
    std::unordered_map<uint32_t,uint64_t> uses;Scene s;
    while(next(s)) {
        st.scenes++;st.references+=s.ids.size();st.auxBytes+=s.aux.size();if(keep)st.timestamps.push_back(s.timestamp);
        for(auto id:s.ids)uses[id]++;
    }
    for(auto [id,n]:uses)if(auto c=command(id))for(auto& i:c->instructions)st.opcodes[i.opcode]+=n;
    st.timelineCrc=timeline_->checksum;st.exactEof=true;return st;
}
}
