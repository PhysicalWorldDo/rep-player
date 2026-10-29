#include "movies.hpp"
#include "runtime_paths.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace rep {
static std::wstring quote(std::wstring value);
struct Movies::Payload {
    std::filesystem::path path;bool owned=false;
    ~Payload(){if(owned){std::error_code error;std::filesystem::remove(path,error);}}
};
struct Movies::Library {
    HMODULE module=nullptr;
    ~Library(){if(module)FreeLibrary(module);}
};
struct Movies::Decoder {
    std::shared_ptr<Payload> payload;
    MovieInfo info;std::filesystem::path path;std::shared_ptr<Frame> frame;
    void* bink=nullptr;std::shared_ptr<Library> library;HANDLE pipe=nullptr,process=nullptr;
    uint32_t rateNumerator=0,rateDenominator=1;
    long double timeOrigin=0;
    int last=-1,next=0;
    ~Decoder(){closePipe();if(bink){auto fn=reinterpret_cast<void(*)(void*)>(GetProcAddress(library->module,"BinkClose"));fn(bink);}}
    void closePipe(){if(pipe)CloseHandle(pipe);pipe=nullptr;if(process){if(WaitForSingleObject(process,20)==WAIT_TIMEOUT){TerminateProcess(process,0);WaitForSingleObject(process,2000);}CloseHandle(process);}process=nullptr;}
    void startPipe(const std::filesystem::path& ffmpeg,const std::wstring& arguments){
        closePipe();SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};HANDLE writer;
        if(!CreatePipe(&pipe,&writer,&attributes,0))throw Error("CreatePipe movie failed");
        SetHandleInformation(pipe,HANDLE_FLAG_INHERIT,0);
        HANDLE nullFile=CreateFileW(L"NUL",GENERIC_WRITE|GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&attributes,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdOutput=writer;startup.StartupInfo.hStdError=startup.StartupInfo.hStdInput=nullFile;PROCESS_INFORMATION child{};
        SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<uint8_t> attributesStorage(size);
        startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributesStorage.data());
        if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&size)){CloseHandle(writer);CloseHandle(nullFile);closePipe();throw Error("cannot initialize movie decoder handles");}
        HANDLE inherited[]{writer,nullFile};bool configured=UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr);
        auto command=quote(ffmpeg.wstring())+L" -v error -nostdin "+arguments;
        bool started=configured&&CreateProcessW(ffmpeg.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&child);
        DeleteProcThreadAttributeList(startup.lpAttributeList);CloseHandle(writer);CloseHandle(nullFile);
        if(!started){closePipe();throw Error("cannot start bundled native AVI decoder");}
        CloseHandle(child.hThread);process=child.hProcess;
    }
    void readTimeOrigin(const std::filesystem::path& ffmpeg){
        // AVI codecs with B frames can start presentation one or more samples
        // after container time zero. Read one decoded frame's original PTS; the
        // existing FFmpeg binary supplies it without scanning the movie.
        Decoder probe;
        probe.startPipe(ffmpeg,L"-copyts -start_at_zero -i "+quote(path.wstring())+L" -map 0:v:0 -frames:v 1 -f framecrc pipe:1");
        std::string metadata;char buffer[1024];DWORD count=0;
        while(ReadFile(probe.pipe,buffer,sizeof(buffer),&count,nullptr)&&count)metadata.append(buffer,count);
        std::istringstream lines(metadata);lines.imbue(std::locale::classic());std::string line;int64_t numerator=0,denominator=0;
        while(std::getline(lines,line)){
            if(line.starts_with("#tb 0:")){std::replace(line.begin(),line.end(),'/',' ');std::istringstream values(line.substr(6));values>>numerator>>denominator;}
            else if(!line.empty()&&line[0]!='#'){
                std::replace(line.begin(),line.end(),',',' ');std::istringstream values(line);int stream=0;int64_t dts=0,pts=0;
                if((values>>stream>>dts>>pts)&&numerator&&denominator){timeOrigin=static_cast<long double>(pts)*numerator/denominator;return;}
            }
        }
        throw Error("cannot read AVI first presentation timestamp");
    }
};
struct Movies::Instance {
    std::string logical;
    std::shared_ptr<Decoder> decoder;
    std::shared_ptr<Frame> frame;
    MovieInfo info;
    uint32_t position=0;
};
struct Movies::State {std::unordered_map<uint64_t,Instance> instances;};
Movies::Movies(std::filesystem::path client,std::filesystem::path cache):client_(std::move(client)),cache_(std::move(cache)){
    std::filesystem::create_directories(cache_);ffmpeg_=ffmpegExecutable();state_=std::make_shared<State>();
}
Movies::~Movies(){reset();}
void Movies::writable(){if(state_.use_count()!=1)state_=std::make_shared<State>(*state_);}
void Movies::reset(){transactionBase_.reset();state_=std::make_shared<State>();decoders_.clear();payloads_.clear();}
Movies::Snapshot Movies::snapshot()const{
    auto logical=std::make_shared<State>(*state_);
    // Recipes retain source identity and recorded position, not decoded movie
    // frames, child processes or extracted payload files from earlier scenes.
    for(auto& [id,instance]:logical->instances){instance.decoder.reset();instance.frame.reset();}
    return Snapshot(std::move(logical));
}
void Movies::restoreSnapshot(const Snapshot& snapshot){state_=snapshot.state_?snapshot.state_:std::make_shared<State>();}
void Movies::beginTransaction(){if(transactionBase_)throw Error("nested movie transaction");transactionBase_=state_;}
void Movies::commitTransaction(){transactionBase_.reset();}
void Movies::rollbackTransaction(){if(transactionBase_)state_=std::move(transactionBase_);}
std::vector<std::shared_ptr<Pixels>> Movies::pixels()const{std::vector<std::shared_ptr<Pixels>> out;for(auto& [id,instance]:state_->instances)if(instance.frame)out.push_back(instance.frame->texture);return out;}
std::shared_ptr<Pixels> Movies::pixels(uint64_t id)const{auto it=state_->instances.find(id);return it==state_->instances.end()||!it->second.frame?nullptr:it->second.frame->texture;}
void Movies::stop(uint64_t id){writable();state_->instances.erase(id);}
MovieInfo Movies::info(uint64_t id)const{auto it=state_->instances.find(id);return it==state_->instances.end()?MovieInfo{}:it->second.info;}
uint32_t Movies::color(uint64_t id,uint32_t v,bool modern)const{return modern||!info(id).bink?0xffffffffu:((v&255)<<24)|0xffffff;}
std::shared_ptr<Movies::Payload> Movies::payload(std::string logical){
    logical=canonical(logical);if(auto it=payloads_.find(logical);it!=payloads_.end())if(auto active=it->second.lock())return active;
    auto original=client_/wide(logical);if(!std::filesystem::is_regular_file(original))return {};
    auto result=std::make_shared<Payload>();result->path=original;
    auto data=readFile(original);size_t offset=0,size=0;
    if(data.size()>=19&&!std::memcmp(data.data(),"Neople Video Stream",19)){
        // Stream stores a 1024-byte seed after the 19-byte magic. Rotate its
        // 8192 bits right by block index + 1, then XOR each payload block.
        offset=19+1024;
        if(data.size()<offset)throw Error("invalid Neople Video Stream header");
        for(size_t pos=offset;pos<data.size();pos+=1024){
            size_t shift=((pos-offset)/1024+1)&0x1fff,byteShift=shift>>3,bitShift=shift&7;
            uint8_t mask=uint8_t((1u<<bitShift)-1);
            for(size_t i=0;i<std::min<size_t>(1024,data.size()-pos);i++){
                uint8_t cur=data[19+((i-byteShift)&0x3ff)],prev=data[19+((i-byteShift-1)&0x3ff)];
                uint8_t key=uint8_t((cur>>bitShift)|((prev&mask)<<(8-bitShift)));
                data[pos+i]^=key;
            }
        }
        if(data.size()-offset<8||std::memcmp(data.data()+offset,"KB2n",4))throw Error("invalid decrypted Neople Video Stream payload");
        size=uint64_t(at<uint32_t>(data,offset+4))+8;
        if(size<=8||size>512ull*1024*1024||size>data.size()-offset)throw Error("invalid Neople Video Stream payload size");
    }else if(data.size()>=32&&!std::memcmp(data.data(),"Neople Video Fil",16)){
        uint32_t version=at<uint32_t>(data,16),count=at<uint32_t>(data,20),padded=at<uint32_t>(data,28);size=at<uint32_t>(data,24);offset=32;
        if(version!=1||count!=1||padded<size||padded%1024||data.size()<32ull+padded)throw Error("invalid Neople movie header");
        for(size_t i=1024;i<padded;i++)data[32+i]^=data[32+i-1024];
    }else{payloads_[logical]=result;return result;}
    // Each decoder group owns a new file. Existing caches and client inputs are
    // never reused or removed; preview and export may decode the same movie.
    static std::atomic<uint64_t> sequence{0};HANDLE file;
    do {
        result->path=cache_/(L"movie-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(sequence++)+original.extension().wstring());
        file=CreateFileW(result->path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,nullptr);
    }while(file==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_FILE_EXISTS);
    if(file==INVALID_HANDLE_VALUE)throw Error("cannot create owned movie payload");result->owned=true;
    size_t position=0;bool okay=true;
    while(position<size){DWORD written=0;DWORD bytes=DWORD(std::min<size_t>(size-position,1024*1024));if(!WriteFile(file,data.data()+offset+position,bytes,&written,nullptr)||!written){okay=false;break;}position+=written;}
    CloseHandle(file);if(!okay)throw Error("cannot save owned movie payload");
    payloads_[logical]=result;return result;
}
static MovieInfo aviInfo(const Bytes& data,uint32_t& numerator,uint32_t& denominator){
    MovieInfo result;
    for(size_t i=12;i+64<=std::min(data.size(),size_t(1024*1024));i+=2){if(std::memcmp(data.data()+i,"avih",4))continue;uint32_t us=at<uint32_t>(data,i+8);result.frames=at<uint32_t>(data,i+24);result.width=at<uint32_t>(data,i+40);result.height=at<uint32_t>(data,i+44);numerator=1000000;denominator=us;result.rate=us?1000000./us:0;break;}
    for(size_t i=12;i+64<=std::min(data.size(),size_t(1024*1024));i+=2){if(std::memcmp(data.data()+i,"strh",4)||std::memcmp(data.data()+i+8,"vids",4))continue;uint32_t scale=at<uint32_t>(data,i+28),rate=at<uint32_t>(data,i+32);if(scale&&rate){numerator=rate;denominator=scale;result.rate=double(rate)/scale;}break;}
    if(!result.width||!result.height||!result.frames||!result.rate)throw Error("invalid AVI stream header");return result;
}
static std::wstring quote(std::wstring v){std::wstring out=L"\"";size_t slash=0;for(wchar_t c:v){if(c==L'\\'){slash++;continue;}if(c==L'\"')out.append(slash*2+1,L'\\');else out.append(slash,L'\\');slash=0;out+=c;}out.append(slash*2,L'\\');out+=L'\"';return out;}
static std::wstring seconds(long double value){std::wostringstream stream;stream.imbue(std::locale::classic());stream<<std::fixed<<std::setprecision(12)<<value;return stream.str();}
std::shared_ptr<Frame> Movies::frame(std::string logical,uint64_t id,uint32_t position){
    bool notified=false;auto wait=[&]{if(!notified){notified=true;if(demandWait_)demandWait_();}};
    logical=canonical(logical);writable();auto& instances=state_->instances;
    auto binding=instances.find(id);
    if(binding==instances.end()||(!logical.empty()&&binding->second.logical!=logical)||!binding->second.decoder){
        if(logical.empty()){if(binding==instances.end())return {};logical=binding->second.logical;}
        if(binding!=instances.end())instances.erase(binding);
        std::shared_ptr<Decoder> instance;
        if(auto it=decoders_.find(logical);it!=decoders_.end())instance=it->second.lock();
        if(!instance){
        wait();
        auto source=payload(logical);if(!source){missing++;return {};}auto path=source->path;
        auto data=readFile(path);instance=std::make_shared<Decoder>();instance->payload=std::move(source);instance->path=path;bool bink=data.size()>=4&&(!std::memcmp(data.data(),"KB2",3)||!std::memcmp(data.data(),"BIK",3));
        if(bink){if(!bink_){auto library=std::make_shared<Library>();library->module=LoadLibraryW((client_/L"bink2w64.dll").c_str());if(!library->module)throw Error("cannot load client Bink2 decoder");bink_=std::move(library);}
            instance->library=bink_;auto open=reinterpret_cast<void*(*)(const char*,uint32_t)>(GetProcAddress(bink_->module,"BinkOpen"));auto native=path.string();
            // CN 14729D1A0 / USA 146EF1760 enable the encoded Alpha plane.
            instance->bink=open(native.c_str(),0x105000u);
            if(!instance->bink)throw Error("BinkOpen failed");auto fields=static_cast<uint32_t*>(instance->bink);instance->info={int(fields[0]),int(fields[1]),int(fields[2]),double(fields[5])/fields[6],true};
            if(auto sound=GetProcAddress(bink_->module,"BinkSetSoundOnOff"))reinterpret_cast<int(*)(void*,int)>(sound)(instance->bink,0);
        }else {instance->info=aviInfo(data,instance->rateNumerator,instance->rateDenominator);instance->readTimeOrigin(ffmpeg_);}
        decoders_[logical]=instance;
        }
        auto info=instance->info;
        binding=instances.emplace(id,Instance{logical,std::move(instance),{},info,position}).first;
    }
    auto& instance=*binding->second.decoder;auto& info=instance.info;
    binding->second.position=position;
    // Movie23 records Bink's one-based frame number, not elapsed milliseconds.
    int requested=info.bink?int(std::min(position?position-1:0u,uint32_t(info.frames-1))):int(std::min(uint64_t(position)*instance.rateNumerator/(uint64_t(instance.rateDenominator)*1000),uint64_t(info.frames-1)));
    if(instance.last==requested){binding->second.frame=instance.frame;return instance.frame;}
    auto frame=std::make_shared<Frame>();auto& f=*frame;f.width=f.fullWidth=info.width;f.height=f.fullHeight=info.height;f.rect={0,0,info.width,info.height};f.texture=std::make_shared<Pixels>();f.texture->width=info.width;f.texture->height=info.height;f.texture->rgba.resize(size_t(info.width)*info.height*4);
    auto& out=f.texture->rgba;
    if(info.bink){auto go=reinterpret_cast<void(*)(void*,uint32_t,uint32_t)>(GetProcAddress(instance.library->module,"BinkGoto"));auto decode=reinterpret_cast<int(*)(void*)>(GetProcAddress(instance.library->module,"BinkDoFrame"));auto copy=reinterpret_cast<void(*)(void*,void*,int32_t,uint32_t,uint32_t,uint32_t,uint32_t)>(GetProcAddress(instance.library->module,"BinkCopyToBuffer"));
        if(instance.last>=0&&requested!=instance.last+1)wait();
        go(instance.bink,requested+1,0);if(decode(instance.bink))throw Error("BinkDoFrame failed");copy(instance.bink,out.data(),info.width*4,info.height,0,0,6);
    }else{
        int sequentialWindow=std::clamp(int(std::ceil(info.rate)),1,32);
        if(requested<instance.next||requested-instance.next>sequentialWindow||!instance.pipe){
            wait();
            // Input-side seek uses the demuxer's index/keyframes. Keep original
            // presentation times after seeking, then select the exact CFR AVI
            // sample. One preceding frame avoids decimal rounding dropping the
            // requested sample; the half-frame boundary handles rational rates.
            long double step=static_cast<long double>(instance.rateDenominator)/instance.rateNumerator;
            auto threshold=seconds(instance.timeOrigin+(static_cast<long double>(requested)-.5L)*step);
            auto seek=requested>1?L" -ss "+seconds(instance.timeOrigin+(requested-1)*step):L"";
            instance.startPipe(ffmpeg_,L"-copyts -start_at_zero"+seek+L" -i "+quote(instance.path.wstring())+L" -map 0:v:0 -vf \"select=gte(t\\,"+threshold+L")\" -fps_mode passthrough -f rawvideo -pix_fmt rgba pipe:1");
            instance.next=requested;
        }
        while(instance.next<=requested){size_t pos=0;while(pos<out.size()){DWORD count=0;if(!ReadFile(instance.pipe,out.data()+pos,DWORD(out.size()-pos),&count,nullptr)||!count){instance.closePipe();throw Error("native AVI frame decode failed");}pos+=count;}instance.next++;}
    }
    instance.last=requested;instance.frame=std::move(frame);binding->second.frame=instance.frame;decoded++;return instance.frame;
}
}
