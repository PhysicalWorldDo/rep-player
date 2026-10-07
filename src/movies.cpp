#include "movies.hpp"
#include "runtime_paths.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <sstream>

namespace rep {
struct Movies::Payload {
    std::filesystem::path path;bool owned=false;
    ~Payload(){if(owned){std::error_code error;std::filesystem::remove(path,error);}}
};
struct Movies::Instance {
    std::shared_ptr<Payload> payload;
    MovieInfo info;std::filesystem::path path;std::shared_ptr<Frame> frame;
    void* bink=nullptr;HMODULE library=nullptr;HANDLE pipe=nullptr,process=nullptr;
    int last=-1,next=0;
    ~Instance(){closePipe();if(bink){auto fn=reinterpret_cast<void(*)(void*)>(GetProcAddress(library,"BinkClose"));fn(bink);}}
    void closePipe(){if(pipe)CloseHandle(pipe);pipe=nullptr;if(process){if(WaitForSingleObject(process,20)==WAIT_TIMEOUT){TerminateProcess(process,0);WaitForSingleObject(process,2000);}CloseHandle(process);}process=nullptr;}
};
Movies::Movies(std::filesystem::path client,std::filesystem::path cache):client_(std::move(client)),cache_(std::move(cache)){
    std::filesystem::create_directories(cache_);ffmpeg_=ffmpegExecutable();
}
Movies::~Movies(){reset();if(bink_)FreeLibrary(bink_);}
void Movies::reset(){instances_.clear();payloads_.clear();}
std::vector<std::shared_ptr<Pixels>> Movies::pixels()const{std::vector<std::shared_ptr<Pixels>> out;for(auto& [id,instance]:instances_)out.push_back(instance->frame->texture);return out;}
std::shared_ptr<Pixels> Movies::pixels(uint64_t id)const{auto it=instances_.find(id);return it==instances_.end()?nullptr:it->second->frame->texture;}
void Movies::stop(uint64_t id){instances_.erase(id);}
MovieInfo Movies::info(uint64_t id)const{auto it=instances_.find(id);return it==instances_.end()?MovieInfo{}:it->second->info;}
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
static MovieInfo aviInfo(const Bytes& data){
    MovieInfo result;
    for(size_t i=12;i+64<=std::min(data.size(),size_t(1024*1024));i+=2){if(std::memcmp(data.data()+i,"avih",4))continue;uint32_t us=at<uint32_t>(data,i+8);result.frames=at<uint32_t>(data,i+24);result.width=at<uint32_t>(data,i+40);result.height=at<uint32_t>(data,i+44);result.rate=us?1000000./us:0;break;}
    for(size_t i=12;i+64<=std::min(data.size(),size_t(1024*1024));i+=2){if(std::memcmp(data.data()+i,"strh",4)||std::memcmp(data.data()+i+8,"vids",4))continue;uint32_t scale=at<uint32_t>(data,i+28),rate=at<uint32_t>(data,i+32);if(scale&&rate)result.rate=double(rate)/scale;break;}
    if(!result.width||!result.height||!result.frames||!result.rate)throw Error("invalid AVI stream header");return result;
}
static std::wstring quote(std::wstring v){std::wstring out=L"\"";size_t slash=0;for(wchar_t c:v){if(c==L'\\'){slash++;continue;}if(c==L'\"')out.append(slash*2+1,L'\\');else out.append(slash,L'\\');slash=0;out+=c;}out.append(slash*2,L'\\');out+=L'\"';return out;}
std::shared_ptr<Frame> Movies::frame(std::string logical,uint64_t id,uint32_t timestamp){
    if(!instances_.contains(id)){
        if(logical.empty())return {};auto source=payload(logical);if(!source){missing++;return {};}auto path=source->path;
        auto data=readFile(path);auto instance=std::make_unique<Instance>();instance->payload=std::move(source);instance->path=path;bool bink=data.size()>=4&&(!std::memcmp(data.data(),"KB2",3)||!std::memcmp(data.data(),"BIK",3));
        if(bink){if(!bink_){bink_=LoadLibraryW((client_/L"bink2w64.dll").c_str());if(!bink_)throw Error("cannot load client Bink2 decoder");}
            instance->library=bink_;auto open=reinterpret_cast<void*(*)(const char*,uint32_t)>(GetProcAddress(bink_,"BinkOpen"));auto native=path.string();instance->bink=open(native.c_str(),0);
            if(!instance->bink)throw Error("BinkOpen failed");auto fields=static_cast<uint32_t*>(instance->bink);instance->info={int(fields[0]),int(fields[1]),int(fields[2]),double(fields[5])/fields[6],true};
            if(auto sound=GetProcAddress(bink_,"BinkSetSoundOnOff"))reinterpret_cast<int(*)(void*,int)>(sound)(instance->bink,0);
        }else instance->info=aviInfo(data);
        instance->frame=std::make_shared<Frame>();auto& f=*instance->frame;auto& m=instance->info;f.width=f.fullWidth=m.width;f.height=f.fullHeight=m.height;f.rect={0,0,m.width,m.height};f.texture=std::make_shared<Pixels>();f.texture->width=m.width;f.texture->height=m.height;f.texture->rgba.resize(size_t(m.width)*m.height*4);
        instances_[id]=std::move(instance);
    }
    auto& instance=*instances_.at(id);auto& info=instance.info;int requested=std::clamp(int(std::floor(timestamp*info.rate/1000)),0,info.frames-1);if(instance.last==requested)return instance.frame;
    auto& out=instance.frame->texture->rgba;
    if(info.bink){auto go=reinterpret_cast<void(*)(void*,uint32_t,uint32_t)>(GetProcAddress(bink_,"BinkGoto"));auto decode=reinterpret_cast<int(*)(void*)>(GetProcAddress(bink_,"BinkDoFrame"));auto copy=reinterpret_cast<void(*)(void*,void*,int32_t,uint32_t,uint32_t,uint32_t,uint32_t)>(GetProcAddress(bink_,"BinkCopyToBuffer"));
        go(instance.bink,requested+1,0);if(decode(instance.bink))throw Error("BinkDoFrame failed");copy(instance.bink,out.data(),info.width*4,info.height,0,0,6);
    }else{
        if(requested<instance.next||!instance.pipe){instance.closePipe();SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};HANDLE writer; if(!CreatePipe(&instance.pipe,&writer,&attributes,0))throw Error("CreatePipe movie failed");SetHandleInformation(instance.pipe,HANDLE_FLAG_INHERIT,0);
            HANDLE nullFile=CreateFileW(L"NUL",GENERIC_WRITE|GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&attributes,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdOutput=writer;startup.StartupInfo.hStdError=startup.StartupInfo.hStdInput=nullFile;PROCESS_INFORMATION process{};
            SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<uint8_t> attributesStorage(size);startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributesStorage.data());
            if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&size)){CloseHandle(writer);CloseHandle(nullFile);instance.closePipe();throw Error("cannot initialize movie decoder handles");}
            HANDLE inherited[]{writer,nullFile};bool configured=UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr);
            auto cmd=quote(ffmpeg_.wstring())+L" -v error -nostdin -i "+quote(instance.path.wstring())+L" -vf \"select=gte(n\\,"+std::to_wstring(requested)+L")\" -fps_mode passthrough -f rawvideo -pix_fmt rgba pipe:1";
            bool started=configured&&CreateProcessW(ffmpeg_.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&process);DeleteProcThreadAttributeList(startup.lpAttributeList);CloseHandle(writer);CloseHandle(nullFile);
            if(!started)throw Error("cannot start bundled native AVI decoder");CloseHandle(process.hThread);instance.process=process.hProcess;instance.next=requested;
        }
        while(instance.next<=requested){size_t pos=0;while(pos<out.size()){DWORD count=0;if(!ReadFile(instance.pipe,out.data()+pos,DWORD(out.size()-pos),&count,nullptr)||!count)throw Error("native AVI frame decode failed");pos+=count;}instance.next++;}
    }
    instance.last=requested;decoded++;return instance.frame;
}
}
