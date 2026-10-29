#include "audio_resources.hpp"
#include "runtime_paths.hpp"
#include "retained_cache.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <future>
#include <semaphore>
#include <thread>

namespace rep {
namespace {
std::string logicalPath(std::string value){
    value=canonical(std::move(value));while(!value.empty()&&value.front()=='/')value.erase(value.begin());return value;
}
std::wstring audioQuote(std::wstring_view value){
    std::wstring result=L"\"";size_t slashes=0;
    for(wchar_t c:value){if(c==L'\\'){slashes++;continue;}result.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;result+=c;}
    result.append(slashes*2,L'\\');result+=L'\"';return result;
}
void utf8Codepoint(std::string& output,uint32_t value){
    if(value<=0x7f)output+=char(value);
    else if(value<=0x7ff){output+=char(0xc0|(value>>6));output+=char(0x80|(value&63));}
    else if(value<=0xffff){output+=char(0xe0|(value>>12));output+=char(0x80|((value>>6)&63));output+=char(0x80|(value&63));}
    else if(value<=0x10ffff){output+=char(0xf0|(value>>18));output+=char(0x80|((value>>12)&63));output+=char(0x80|((value>>6)&63));output+=char(0x80|(value&63));}
}
std::string xmlValue(std::string_view value){
    std::string result;for(size_t i=0;i<value.size();i++){
        if(value[i]!='&'){result+=value[i];continue;}auto end=value.find(';',i+1);if(end==std::string_view::npos){result+='&';continue;}
        auto entity=value.substr(i+1,end-i-1);
        if(entity=="amp")result+='&';else if(entity=="quot")result+='\"';else if(entity=="apos")result+='\'';else if(entity=="lt")result+='<';else if(entity=="gt")result+='>';
        else if(entity.size()>1&&entity[0]=='#'){
            std::string digits(entity.substr(entity[1]=='x'||entity[1]=='X'?2:1));char* tail=nullptr;auto n=std::strtoul(digits.c_str(),&tail,entity[1]=='x'||entity[1]=='X'?16:10);
            if(tail!=digits.c_str()&&!*tail)utf8Codepoint(result,uint32_t(n));else result.append(value.substr(i,end-i+1));
        }else result.append(value.substr(i,end-i+1));i=end;
    }return result;
}
bool whitespace(char c){return c==' '||c=='\t'||c=='\n'||c=='\r';}
using Attributes=std::unordered_map<std::string,std::string>;
Attributes attributes(std::string_view text,size_t position){
    Attributes result;while(position<text.size()){
        while(position<text.size()&&whitespace(text[position]))position++;
        if(position==text.size()||text[position]=='/')break;auto start=position;
        while(position<text.size()&&!whitespace(text[position])&&text[position]!='=')position++;
        auto name=std::string(text.substr(start,position-start));while(position<text.size()&&whitespace(text[position]))position++;
        if(position==text.size()||text[position++]!='=')break;while(position<text.size()&&whitespace(text[position]))position++;
        if(position==text.size()||(text[position]!='\''&&text[position]!='\"'))break;auto quote=text[position++];start=position;
        while(position<text.size()&&text[position]!=quote)position++;result.emplace(std::move(name),xmlValue(text.substr(start,position-start)));if(position<text.size())position++;
    }return result;
}
AudioKind kind(std::string_view name){
    if(name=="VOICE")return AudioKind::Voice;if(name=="EFFECT")return AudioKind::Effect;if(name=="MUSIC")return AudioKind::Music;if(name=="AMBIENT")return AudioKind::Ambient;
    if(name=="UNINTERRUPTED_EFFECT")return AudioKind::UninterruptedEffect;if(name=="RANDOM")return AudioKind::Random;if(name=="GROUP")return AudioKind::Group;if(name=="PLAYTAG")return AudioKind::RandomGroup;return AudioKind::Unknown;
}
double number(const Attributes& values,const std::string& key,double fallback){
    auto it=values.find(key);if(it==values.end())return fallback;char* tail=nullptr;auto value=std::strtod(it->second.c_str(),&tail);
    return tail!=it->second.c_str()&&!*tail&&std::isfinite(value)?value:fallback;
}
double nativeDelay(const Attributes& values,const std::string& key,double fallbackSeconds=0){
    // Native XML sources convert seconds to float milliseconds, then truncate toward zero.
    return int(float(number(values,key,fallbackSeconds)*1000));
}
struct Handle {
    HANDLE value=nullptr;
    Handle()=default;explicit Handle(HANDLE handle):value(handle){}
    ~Handle(){close();}Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    void close(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=nullptr;}
};
struct Decoded {Bytes data;std::string error;bool cancelled=false;};
Decoded decode(const std::filesystem::path& ffmpeg,const Bytes& input,const std::function<bool()>& cancelled,std::atomic<uint64_t>* processCount=nullptr){
    Decoded result;if(cancelled&&cancelled()){result.cancelled=true;return result;}
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};Handle inRead,inWrite,outRead,outWrite,errRead,errWrite;
    if(!CreatePipe(&inRead.value,&inWrite.value,&security,0)||!CreatePipe(&outRead.value,&outWrite.value,&security,0)||!CreatePipe(&errRead.value,&errWrite.value,&security,0))throw Error("cannot create audio decoder pipes");
    SetHandleInformation(inWrite.value,HANDLE_FLAG_INHERIT,0);SetHandleInformation(outRead.value,HANDLE_FLAG_INHERIT,0);SetHandleInformation(errRead.value,HANDLE_FLAG_INHERIT,0);
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput=inRead.value;startup.StartupInfo.hStdOutput=outWrite.value;startup.StartupInfo.hStdError=errWrite.value;
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<uint8_t> storage(bytes);startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes))throw Error("cannot initialize audio decoder handles");
    HANDLE inherited[]{inRead.value,outWrite.value,errWrite.value};bool attributesOkay=UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr);
    auto command=audioQuote(ffmpeg.wstring())+L" -v error -nostdin -i pipe:0 -vn -f f32le -ac 2 -ar 48000 pipe:1";PROCESS_INFORMATION information{};
    bool started=attributesOkay&&CreateProcessW(ffmpeg.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&information);
    DeleteProcThreadAttributeList(startup.lpAttributeList);if(!started)throw Error("cannot start bundled audio decoder");if(processCount)++*processCount;Handle process(information.hProcess),thread(information.hThread);thread.close();inRead.close();outWrite.close();errWrite.close();
    std::atomic<bool> ioFailure=false;
    std::thread writer([&]{size_t position=0;while(position<input.size()){
        DWORD written=0;if(!WriteFile(inWrite.value,input.data()+position,DWORD(std::min<size_t>(65536,input.size()-position)),&written,nullptr)||!written)break;position+=written;
    }inWrite.close();});
    std::thread reader([&]{try{uint8_t block[65536];DWORD received=0;while(ReadFile(outRead.value,block,sizeof(block),&received,nullptr)&&received)result.data.insert(result.data.end(),block,block+received);}catch(...){ioFailure=true;TerminateProcess(process.value,1);}});
    std::thread errors([&]{char block[4096];DWORD received=0;while(ReadFile(errRead.value,block,sizeof(block),&received,nullptr)&&received){if(result.error.size()<32768)result.error.append(block,std::min<size_t>(received,32768-result.error.size()));}});
    try{while(WaitForSingleObject(process.value,25)==WAIT_TIMEOUT){if(cancelled&&cancelled()){result.cancelled=true;TerminateProcess(process.value,1);break;}}}
    catch(...){TerminateProcess(process.value,1);writer.join();reader.join();errors.join();throw;}
    WaitForSingleObject(process.value,INFINITE);writer.join();reader.join();errors.join();DWORD exit=1;GetExitCodeProcess(process.value,&exit);
    if(exit||ioFailure||result.data.empty()||result.data.size()%(sizeof(float)*2)){if(result.error.empty())result.error="audio decoder produced no valid stereo PCM";result.data.clear();}
    return result;
}
// Enable explicit input mapping only where the elementary/container bytes
// prove there is one audio stream. Everything else keeps FFmpeg's original
// single-input automatic best-stream selection.
bool singleAudioStream(const Bytes& input){
    auto has=[&](size_t at,const char* text,size_t size){return at+size<=input.size()&&!std::memcmp(input.data()+at,text,size);};
    if(has(0,"RIFF",4)&&has(8,"WAVE",4)){
        if(input.size()<12||uint64_t(at<uint32_t>(input,4))+8!=input.size())return false;size_t pos=12;unsigned formats=0,data=0;
        while(pos+8<=input.size()){auto length=at<uint32_t>(input,pos+4);if(uint64_t(pos)+8+length>input.size())return false;if(has(pos,"fmt ",4)){
                if(length<16)return false;auto payload=std::span(input).subspan(pos+8,length);auto format=at<uint16_t>(payload,0),channels=at<uint16_t>(payload,2),bits=at<uint16_t>(payload,14);auto rate=at<uint32_t>(payload,4),byteRate=at<uint32_t>(payload,8);auto align=at<uint16_t>(payload,12);
                if(format==0xfffe){static constexpr uint8_t suffix[12]={0,0,16,0,128,0,0,170,0,56,155,113};if(length<40||at<uint16_t>(payload,16)<22||std::memcmp(payload.data()+28,suffix,12))return false;auto subtype=at<uint32_t>(payload,24);if(subtype!=1&&subtype!=3)return false;format=uint16_t(subtype);}
                if(format!=1&&format!=3)return false;if(!channels||!rate||(format==1?(bits!=8&&bits!=16&&bits!=24&&bits!=32):(bits!=32&&bits!=64))||align!=uint64_t(channels)*bits/8||byteRate!=uint64_t(rate)*align)return false;++formats;
            }if(has(pos,"data",4)&&length)++data;pos+=8+length+(length&1);}
        return pos==input.size()&&formats==1&&data>0;
    }
    if(has(0,"fLaC",4)){
        size_t pos=4;bool first=true,last=false;while(!last&&pos+4<=input.size()){auto type=input[pos]&127;last=(input[pos]&128)!=0;size_t size=(size_t(input[pos+1])<<16)|(size_t(input[pos+2])<<8)|input[pos+3];if(first&&(type!=0||size!=34))return false;first=false;pos+=4;if(pos+size>input.size())return false;pos+=size;}return last&&pos+2<=input.size()&&input[pos]==255&&(input[pos+1]&0xfe)==0xf8;
    }
    if(has(0,"OggS",4)){
        size_t pos=0;std::optional<uint32_t> serial;unsigned bos=0;bool audio=false;
        while(pos<input.size()){if(!has(pos,"OggS",4)||pos+27>input.size()||input[pos+4])return false;auto current=at<uint32_t>(input,pos+14);if(serial&&*serial!=current)return false;serial=current;auto segments=input[pos+26];if(pos+27+segments>input.size())return false;size_t bytes=0;for(size_t n=0;n<segments;n++)bytes+=input[pos+27+n];auto payload=pos+27+segments;if(payload+bytes>input.size())return false;if(input[pos+5]&2){if(++bos!=1||pos!=0)return false;audio=has(payload,"\x01vorbis",7)||has(payload,"OpusHead",8);}pos=payload+bytes;}return bos==1&&audio;
    }
    // MP3's pipe input can expose different gapless padding from a regular
    // file, even with seekable=0. Keep its original decoder acquisition.
    return false;
}
struct BatchDecoded {std::vector<Bytes> outputs;bool success=false,cancelled=false;};
BatchDecoded decodeBatch(const std::filesystem::path& ffmpeg,const std::filesystem::path& workRoot,const std::vector<const Bytes*>& inputs,const std::function<bool()>& cancelled,std::atomic<uint64_t>& processCount){
    BatchDecoded result;if(cancelled&&cancelled()){result.cancelled=true;return result;}
    static std::counting_semaphore<4> slots(4);
    while(!slots.try_acquire_for(std::chrono::milliseconds(2)))if(cancelled&&cancelled()){result.cancelled=true;return result;}
    struct Permit {std::counting_semaphore<4>& slots;~Permit(){slots.release();}} permit{slots};
    struct Files {
        std::filesystem::path directory;std::vector<std::filesystem::path> files;
        ~Files(){for(const auto& file:files)if(file.parent_path()==directory)DeleteFileW(file.c_str());if(!directory.empty())RemoveDirectoryW(directory.c_str());}
    } owned;
    auto parent=workRoot/L"audio-batches";std::filesystem::create_directories(parent);static std::atomic<uint64_t> sequence=0;
    for(;;){auto candidate=parent/(std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++sequence));if(CreateDirectoryW(candidate.c_str(),nullptr)){owned.directory=std::move(candidate);break;}if(GetLastError()!=ERROR_ALREADY_EXISTS)throw Error("cannot create private audio batch directory");}
    std::wstring command=audioQuote(ffmpeg.wstring())+L" -v error -nostdin -y";std::vector<std::filesystem::path> outputs;
    for(size_t n=0;n<inputs.size();n++){if(cancelled&&cancelled()){result.cancelled=true;return result;}auto path=owned.directory/(L"input-"+std::to_wstring(n)+L".bin");owned.files.push_back(path);std::ofstream file(path,std::ios::binary);
        for(size_t position=0;position<inputs[n]->size();){if(cancelled&&cancelled()){result.cancelled=true;return result;}auto length=std::min<size_t>(1048576,inputs[n]->size()-position);file.write(reinterpret_cast<const char*>(inputs[n]->data()+position),std::streamsize(length));if(!file)throw Error("cannot write audio batch input");position+=length;}
        // Match the original non-seekable pipe acquisition for qualified
        // formats. MP3 is excluded above because this alone cannot match it.
        command+=L" -threads 1 -seekable 0 -i "+audioQuote(path.wstring());}
    for(size_t n=0;n<inputs.size();n++){auto path=owned.directory/(L"output-"+std::to_wstring(n)+L".f32");outputs.push_back(path);owned.files.push_back(path);command+=L" -map "+std::to_wstring(n)+L":a:0 -vn -f f32le -ac 2 -ar 48000 "+audioQuote(path.wstring());}
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION info{};
    if(!CreateProcessW(ffmpeg.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,owned.directory.c_str(),&startup,&info))throw Error("cannot start bundled audio batch decoder");++processCount;Handle process(info.hProcess),thread(info.hThread);thread.close();
    try{while(WaitForSingleObject(process.value,10)==WAIT_TIMEOUT)if(cancelled&&cancelled()){result.cancelled=true;TerminateProcess(process.value,1);break;}}
    catch(...){TerminateProcess(process.value,1);WaitForSingleObject(process.value,INFINITE);throw;}
    WaitForSingleObject(process.value,INFINITE);DWORD exit=1;GetExitCodeProcess(process.value,&exit);if(exit||result.cancelled)return result;
    for(const auto& path:outputs){if(cancelled&&cancelled()){result.cancelled=true;result.outputs.clear();return result;}auto bytes=readFile(path,cancelled);if(bytes.empty()||bytes.size()%(sizeof(float)*2)){result.outputs.clear();return result;}result.outputs.push_back(std::move(bytes));}
    result.success=true;return result;
}
}
struct AudioResources::PackageTable {std::unordered_map<std::string,Entry> entries;};
struct AudioResources::Shared {
    std::mutex mutex;
    std::unordered_map<std::string,std::shared_ptr<Pending>> pending;
    std::unordered_set<std::string> unavailable;
    struct Source {std::string key;bool absent=false,patch=false;};
    std::shared_ptr<RetainedCache> fallbackCache=std::make_shared<RetainedCache>();
};
struct AudioResources::Index {
    uint64_t identity=0;
    std::shared_ptr<Shared> shared=std::make_shared<Shared>();
    std::filesystem::path client;
    std::unordered_map<std::string,SoundDefinition> definitions;
    std::unordered_map<std::string,Entry> patches;
    std::vector<std::filesystem::path> officialPackages;
    std::vector<std::string> diagnostics;
    uint64_t missing=0;
    // Keep a failed path on the official source for every reader until refresh.
    struct RejectedPatches {std::mutex mutex;std::unordered_set<std::string> paths;};
    std::shared_ptr<RejectedPatches> rejected=std::make_shared<RejectedPatches>();
};
AudioResources::AudioResources(std::filesystem::path clientRoot,std::filesystem::path cacheRoot,bool patchesEnabled,std::shared_ptr<RetainedCache> cache)
    :AudioResources(buildIndex(std::move(clientRoot),patchesEnabled),std::move(cache),std::move(cacheRoot)){}
AudioResources::AudioResources(IndexPtr index,std::shared_ptr<RetainedCache> cache,std::filesystem::path workRoot):index_(std::move(index)),ffmpeg_(ffmpegExecutable()),workRoot_(workRoot.empty()?workRoot:std::filesystem::absolute(workRoot).lexically_normal()){
    if(!index_)throw Error("sound resource index is absent");
    shared_=index_->shared;cache_=cache?std::move(cache):shared_->fallbackCache;
    diagnostics_=index_->diagnostics;missing_=index_->missing;
}
AudioResources::IndexPtr AudioResources::buildIndex(std::filesystem::path clientRoot,bool patchesEnabled){
    static std::atomic<uint64_t> nextIdentity=0;
    auto index=std::make_shared<Index>();index->identity=++nextIdentity;index->client=std::move(clientRoot);loadDefinitions(*index);
    auto directory=index->client/L"SoundPacks";std::vector<std::filesystem::path> packages;
    if(std::filesystem::is_directory(directory))for(const auto& item:std::filesystem::directory_iterator(directory))
        if(item.is_regular_file()&&canonical(utf8(item.path().extension().wstring()))==".npk")packages.push_back(item.path());
    // Match ImagePacks2: preserve case and compare unsigned UTF-16 filenames.
    std::sort(packages.begin(),packages.end(),[](const auto& a,const auto& b){
        auto x=a.filename().native(),y=b.filename().native();
        return std::lexicographical_compare(x.begin(),x.end(),y.begin(),y.end(),[](wchar_t x,wchar_t y){return uint16_t(x)<uint16_t(y);});
    });
    auto official=[](const auto& path){return canonical(utf8(path.filename().wstring())).rfind("sounds",0)==0;};
    auto boundary=std::find_if(packages.begin(),packages.end(),official);
    if(patchesEnabled&&boundary!=packages.end())for(auto it=packages.begin();it!=boundary;++it)readPackage(*it,index->patches,true);
    for(const auto& package:packages)if(official(package))index->officialPackages.push_back(package);
    return index;
}
void AudioResources::report(const std::string& key,std::string message,bool missing){
    std::lock_guard lock(mutex_);if(reported_.insert(key).second){diagnostics_.push_back(std::move(message));if(missing)missing_++;}
}
void AudioResources::loadDefinitions(Index& index){
    auto& definitions_=index.definitions;
    auto report=[&](const char*,std::string message,bool missing){index.diagnostics.push_back(std::move(message));if(missing)index.missing++;};
    auto path=index.client/L"audio.xml";if(!std::filesystem::is_regular_file(path)){report("registry","Client audio.xml is unavailable",true);return;}
    try{
        auto bytes=readFile(path);std::string_view xml(reinterpret_cast<const char*>(bytes.data()),bytes.size());size_t position=0;std::vector<std::string> sources;
        while((position=xml.find('<',position))!=std::string_view::npos){
            if(xml.substr(position,4)=="<!--"){auto end=xml.find("-->",position+4);position=end==std::string_view::npos?xml.size():end+3;continue;}
            if(xml.substr(position,9)=="<![CDATA["){auto end=xml.find("]]>",position+9);position=end==std::string_view::npos?xml.size():end+3;continue;}
            size_t start=++position;if(position==xml.size())break;if(xml[position]=='/'||xml[position]=='?'||xml[position]=='!'){if(xml[position]=='/'&&!sources.empty())sources.pop_back();auto end=xml.find('>',position);position=end==std::string_view::npos?xml.size():end+1;continue;}
            while(position<xml.size()&&!whitespace(xml[position])&&xml[position]!='/'&&xml[position]!='>')position++;auto element=xml.substr(start,position-start);auto type=kind(element);auto attributeStart=position;
            char quote=0;while(position<xml.size()){char c=xml[position];if(quote){if(c==quote)quote=0;}else if(c=='\''||c=='\"')quote=c;else if(c=='>')break;position++;}
            auto body=xml.substr(attributeStart,position-attributeStart);auto last=body.find_last_not_of(" \t\r\n");bool selfClosing=last!=std::string_view::npos&&body[last]=='/';auto values=attributes(body,0);if(position<xml.size())position++;
            std::string source=sources.empty()?std::string{}:sources.back();
            if(element=="ITEM"&&!source.empty()&&values.contains("TAG")){auto active=definitions_.find(source);if(active!=definitions_.end()&&active->second.kind==AudioKind::Group)
                active->second.children.push_back({values["TAG"],nativeDelay(values,"DELAY"),nativeDelay(values,"DELAY_RANGE")});}
            if(type!=AudioKind::Unknown&&values.contains("ID")){
                SoundDefinition definition;definition.tag=values["ID"];definition.kind=type;definition.file=values["FILE"];definition.nativeFiltered=type==AudioKind::Random||type==AudioKind::RandomGroup;
                definition.playable=(type==AudioKind::Voice||type==AudioKind::Effect||type==AudioKind::Music||type==AudioKind::Ambient||type==AudioKind::UninterruptedEffect)&&!definition.file.empty();
                definition.loopDelay=nativeDelay(values,"LOOP_DELAY",-.001);definition.loopDelayRange=nativeDelay(values,"LOOP_DELAY_RANGE");
                definition.loopCount=int(std::clamp(number(values,"LOOP_TIMES",0),0.,double(INT_MAX)));definition.duplicateLimit=int(std::clamp(number(values,"DUPLICATE_LIMIT",0),0.,double(INT_MAX)));definition.duplicatePolicy=values["DUPLICATE_POLICY"];
                definition.volumeAdjust=number(values,"VOLUME_ADJUST",0);definition.volumeAdjustRange=number(values,"VOLUME_ADJUST_RANGE",0);
                auto nativeAdjust=int(std::clamp(definition.volumeAdjust*100,double(INT_MIN),double(INT_MAX)));definition.gain=float(std::clamp(int(nativeAdjust*.01)+100,0,100))*.01f;
                definition.fadeData=values["FADE_DATA"];definition.sidechainData=values["SIDECHAIN_DATA"];definition.ignore3dSound=values["IGNORE_3DSOUND"]=="TRUE";
                source=definition.tag;definitions_.try_emplace(source,std::move(definition));
            }
            if(!selfClosing)sources.push_back(std::move(source));
        }
    }catch(const std::exception& error){report("registry","Cannot read audio.xml: "+std::string(error.what()),true);}
}
const SoundDefinition* AudioResources::definition(const std::string& tag)const{
    auto it=index_->definitions.find(tag);return it==index_->definitions.end()?nullptr:&it->second;
}
std::vector<std::string> AudioResources::diagnostics()const{std::lock_guard lock(mutex_);return diagnostics_;}
std::shared_ptr<const AudioResources::PackageTable> AudioResources::indexPackage(const std::filesystem::path& package){
    auto key="audio/table/"+std::to_string(index_->identity)+"/"+canonical(utf8(package.wstring()));
    std::lock_guard lock(shared_->mutex);
    if(auto table=cache_->find<PackageTable>(key))return table;
    auto table=std::make_shared<PackageTable>();readPackage(package,table->entries);size_t bytes=sizeof(PackageTable);
    for(const auto& [name,entry]:table->entries)bytes+=sizeof(entry)+sizeof(name)+name.capacity()+entry.package.native().capacity()*sizeof(wchar_t)+sizeof(void*)*4;
    cache_->insert<PackageTable>(key,table,bytes);return table;
}
void AudioResources::readPackage(const std::filesystem::path& package,std::unordered_map<std::string,Entry>& entries,bool patch){
    auto packageKey=canonical(utf8(package.filename().wstring()));
    std::ifstream file(package,std::ios::binary);if(!file)return;std::array<uint8_t,20> header{};file.read(reinterpret_cast<char*>(header.data()),header.size());
    if(!file||std::memcmp(header.data(),"NeoplePack_Bill\0",16))throw Error("invalid sound NPK header: "+packageKey);
    uint32_t count=at<uint32_t>(header,16);file.seekg(0,std::ios::end);auto size=file.tellg();if(size<20||uint64_t(count)*264>uint64_t(size)-20)throw Error("truncated sound NPK table: "+packageKey);
    file.seekg(20);Bytes table(size_t(count)*264);file.read(reinterpret_cast<char*>(table.data()),table.size());if(!file)throw Error("cannot read sound NPK table: "+packageKey);
    std::string seed="puchikon@neople dungeon and fighter ";while(seed.size()<255)seed+="DNF";seed.resize(255);seed.push_back(0);
    for(uint32_t i=0;i<count;i++){
        auto record=std::span(table).subspan(size_t(i)*264,264);std::string logical;for(size_t j=0;j<256;j++){char c=char(record[8+j]^uint8_t(seed[j]));if(!c)break;logical+=c;}
        auto offset=at<uint32_t>(record,0),length=at<uint32_t>(record,4);if(uint64_t(offset)+length>uint64_t(size))throw Error("sound NPK entry exceeds package: "+packageKey);
        entries.try_emplace(logicalPath(std::move(logical)),Entry{package,offset,length,patch});
    }
}
std::optional<AudioResources::Entry> AudioResources::resolve(const std::string& logical,bool original){
    if(!original)if(auto it=index_->patches.find(logical);it!=index_->patches.end()){
        std::lock_guard rejectedLock(index_->rejected->mutex);if(!index_->rejected->paths.contains(logical))return it->second;
    }
    auto directory=index_->client/L"SoundPacks";auto parent=logical.substr(0,logical.find_last_of('/'));
    while(parent.find('/')!=std::string::npos){auto name=parent;std::replace(name.begin(),name.end(),'/','_');auto package=directory/wide(name+".npk");if(std::filesystem::is_regular_file(package)){auto table=indexPackage(package);if(auto it=table->entries.find(logical);it!=table->entries.end())return it->second;}parent=parent.substr(0,parent.find_last_of('/'));}
    for(const auto& package:index_->officialPackages){auto table=indexPackage(package);if(auto it=table->entries.find(logical);it!=table->entries.end())return it->second;}
    return {};
}
std::shared_ptr<const AudioClip> AudioResources::clip(const std::string& tag,const std::function<bool()>& cancelled){
    if(cancelled&&cancelled())return {};auto source=definition(tag);if(!source){report("tag:"+tag,"Unknown audio TAG: "+tag,true);return {};}if(!source->playable)return {};
    if(auto known=prepared(tag);known.ready){if(!known.clip)report("file:"+logicalPath(source->file),"Audio TAG "+tag+": resource is unavailable",true);return known.clip;}
    auto file=logicalPath(source->file);auto sourceKey="audio/source/"+std::to_string(index_->identity)+"/"+file;
    auto origin=std::make_shared<Shared::Source>();std::optional<Entry> entry;
    try{
        if(file.rfind("sounds/",0)==0){entry=resolve(file);if(entry){origin->key=canonical(utf8(entry->package.wstring()))+"/"+std::to_string(entry->offset)+"/"+std::to_string(entry->length);origin->patch=entry->patch;}else origin->absent=true;}
        else origin->key=canonical(utf8((index_->client/wide(source->file)).wstring()));
    }catch(const std::exception& error){
        if(cancelled&&cancelled())return {};origin->absent=true;report("file:"+file,"Audio TAG "+tag+": "+error.what(),true);
        cache_->insert<Shared::Source>(sourceKey,origin,sizeof(Shared::Source)+sourceKey.capacity());return {};
    }
    auto key="audio/pcm/"+std::to_string(index_->identity)+"/"+origin->key;
    cache_->insert<Shared::Source>(sourceKey,origin,sizeof(Shared::Source)+origin->key.capacity()+sourceKey.capacity());
    if(origin->absent){report("file:"+file,"Audio TAG "+tag+": sound resource is absent from SoundPacks: "+source->file,true);return {};}
    if(auto found=cache_->find<AudioClip>(key))return found;
    std::shared_ptr<Pending> pending;bool owner=false;
    {std::lock_guard lock(shared_->mutex);if(shared_->unavailable.contains(key))return {};
        auto [it,inserted]=shared_->pending.try_emplace(key,std::make_shared<Pending>());pending=it->second;owner=inserted;}
    if(!owner){std::unique_lock lock(pending->mutex);while(!pending->done){if(cancelled&&cancelled())return {};pending->ready.wait_for(lock,std::chrono::milliseconds(10));}auto result=pending->result;bool retry=pending->cancelled;lock.unlock();if(retry&&!(cancelled&&cancelled()))return clip(tag,cancelled);return result;}
    std::shared_ptr<const AudioClip> result;bool wasCancelled=false;
    bool fellBack=false;
    try{
        auto readEntry=[](const Entry& entry){Bytes input(entry.length);std::ifstream stream(entry.package,std::ios::binary);stream.seekg(entry.offset);
            stream.read(reinterpret_cast<char*>(input.data()),input.size());if(!stream)throw Error("cannot read sound NPK payload");return input;};
        auto decodeClip=[&](const Bytes& input){
            auto decoded=decode(ffmpeg_,input,cancelled,&decoderProcesses_);if(decoded.cancelled)return std::shared_ptr<const AudioClip>{};
            if(decoded.data.empty())throw Error("audio decode failed: "+decoded.error);
            auto clip=std::make_shared<AudioClip>();clip->samples.resize(decoded.data.size()/sizeof(float));std::memcpy(clip->samples.data(),decoded.data.data(),decoded.data.size());
            if(!std::all_of(clip->samples.begin(),clip->samples.end(),[](float sample){return std::isfinite(sample);}))throw Error("audio decoder returned non-finite PCM");
            return std::shared_ptr<const AudioClip>(std::move(clip));
        };
        if(file.rfind("sounds/",0)==0){
            try{result=decodeClip(readEntry(*entry));}
            catch(...){
                if(!entry->patch||(cancelled&&cancelled()))throw;
                {std::lock_guard lock(index_->rejected->mutex);index_->rejected->paths.insert(file);}
                fellBack=true;result=clip(tag,cancelled);
            }
        }else{auto path=index_->client/wide(source->file);if(!std::filesystem::is_regular_file(path))throw Error("audio file is absent: "+source->file);result=decodeClip(readFile(path));}
        wasCancelled=!result;if(result&&!fellBack)decoded_++;
    }catch(const std::exception& error){wasCancelled=cancelled&&cancelled();if(!wasCancelled)report("file:"+file,"Audio TAG "+tag+": "+error.what(),true);}
    if(cancelled&&cancelled()){result.reset();wasCancelled=true;}
    if(result&&!fellBack)cache_->insert<AudioClip>(key,result,sizeof(AudioClip)+result->samples.capacity()*sizeof(float));
    {std::lock_guard lock(shared_->mutex);if(!result&&!wasCancelled)shared_->unavailable.insert(key);shared_->pending.erase(key);}
    {std::lock_guard lock(pending->mutex);pending->result=result;pending->cancelled=wasCancelled;pending->done=true;}pending->ready.notify_all();return result;
}
AudioResources::ClipState AudioResources::prepared(const std::string& tag){
    auto source=definition(tag);if(!source){report("tag:"+tag,"Unknown audio TAG: "+tag,true);return {true,{}};}if(!source->playable)return {true,{}};auto file=logicalPath(source->file);
    auto origin=cache_->find<Shared::Source>("audio/source/"+std::to_string(index_->identity)+"/"+file);if(!origin)return {};
    if(origin->patch){std::lock_guard lock(index_->rejected->mutex);if(index_->rejected->paths.contains(file))return {};}
    if(origin->absent){report("file:"+file,"Audio TAG "+tag+": resource is unavailable",true);return {true,{}};}
    auto key="audio/pcm/"+std::to_string(index_->identity)+"/"+origin->key;
    if(auto clip=cache_->find<AudioClip>(key))return {true,std::move(clip)};
    bool unavailable;{std::lock_guard lock(shared_->mutex);unavailable=shared_->unavailable.contains(key);}if(unavailable)report("file:"+file,"Audio TAG "+tag+": resource is unavailable",true);return {unavailable,{}};
}
void AudioResources::trimCache(){cache_->trim();}
std::vector<std::shared_ptr<const AudioClip>> AudioResources::prepareClips(const std::vector<std::string>& tags,const std::function<bool()>& cancelled){
    struct Work {std::string tag,file,key;std::shared_ptr<Pending> pending;Bytes input;std::vector<size_t> aliases;bool batch=false,settled=false,patch=false;};
    std::vector<std::shared_ptr<const AudioClip>> results(tags.size());std::vector<std::unique_ptr<Work>> work;std::unordered_map<std::string,Work*> localOwners;
    std::atomic<bool> aborted=false;std::mutex cancellationMutex;
    auto stopped=[&]{std::lock_guard lock(cancellationMutex);if(aborted)return true;if(cancelled&&cancelled()){aborted=true;return true;}return false;};
    auto settle=[&](Work& item,std::shared_ptr<const AudioClip> result,bool retry){
        if(result)cache_->insert<AudioClip>(item.key,result,sizeof(AudioClip)+result->samples.capacity()*sizeof(float));
        if(item.pending&&!item.settled){
            {std::lock_guard lock(shared_->mutex);auto found=shared_->pending.find(item.key);if(found!=shared_->pending.end()&&found->second==item.pending)shared_->pending.erase(found);}
            {std::lock_guard lock(item.pending->mutex);item.pending->result=result;item.pending->cancelled=retry;item.pending->done=true;}item.pending->ready.notify_all();item.settled=true;
        }
        for(auto index:item.aliases)results[index]=result;
    };
    struct Cleanup {std::function<void()> run;~Cleanup(){run();}} cleanup{[&]{for(auto& item:work)if(item->pending&&!item->settled)settle(*item,{},true);}};
    for(size_t n=0;n<tags.size()&&!stopped();n++){
        if(auto found=prepared(tags[n]);found.ready){results[n]=std::move(found.clip);continue;}
        auto item=std::make_unique<Work>();item->tag=tags[n];item->aliases.push_back(n);auto definition=AudioResources::definition(item->tag);if(!definition||!definition->playable)continue;item->file=logicalPath(definition->file);
        auto source=std::make_shared<Shared::Source>();std::optional<Entry> entry;auto sourceKey="audio/source/"+std::to_string(index_->identity)+"/"+item->file;
        try{
            if(item->file.rfind("sounds/",0)==0){entry=resolve(item->file);if(entry){source->key=canonical(utf8(entry->package.wstring()))+"/"+std::to_string(entry->offset)+"/"+std::to_string(entry->length);source->patch=entry->patch;}else source->absent=true;}
            else source->key=canonical(utf8((index_->client/wide(definition->file)).wstring()));
        }catch(...){item->settled=true;work.push_back(std::move(item));continue;}
        if(source->absent){item->settled=true;work.push_back(std::move(item));continue;}
        item->key="audio/pcm/"+std::to_string(index_->identity)+"/"+source->key;item->patch=source->patch;
        cache_->insert<Shared::Source>(sourceKey,source,sizeof(Shared::Source)+source->key.capacity()+sourceKey.capacity());
        if(auto cached=cache_->find<AudioClip>(item->key)){results[n]=std::move(cached);continue;}
        if(auto own=localOwners.find(item->key);own!=localOwners.end()){own->second->aliases.push_back(n);continue;}
        bool owner=false;
        {std::lock_guard lock(shared_->mutex);if(!shared_->unavailable.contains(item->key)){auto [it,inserted]=shared_->pending.try_emplace(item->key,std::make_shared<Pending>());item->pending=it->second;owner=inserted;}}
        if(!owner){item->pending.reset();item->settled=true;work.push_back(std::move(item));continue;}
        auto* current=item.get();localOwners.emplace(item->key,current);work.push_back(std::move(item));
        try{
            if(workRoot_.empty()){settle(*current,{},true);continue;}
            if(entry){std::ifstream input(entry->package,std::ios::binary);input.seekg(entry->offset);current->input.resize(entry->length);size_t position=0;
                while(position<current->input.size()){if(stopped())break;auto count=std::min<size_t>(1048576,current->input.size()-position);input.read(reinterpret_cast<char*>(current->input.data()+position),std::streamsize(count));if(!input)throw Error("cannot read sound NPK batch payload");position+=count;}}
            else current->input=readFile(index_->client/wide(definition->file),stopped);
            current->batch=!stopped()&&singleAudioStream(current->input);if(!current->batch){current->input.clear();settle(*current,{},true);}
        }catch(...){current->input.clear();settle(*current,{},true);}
    }
    std::vector<Work*> batchable;std::vector<std::function<void()>> operations;
    auto fallback=[&](Work& item){if(!item.settled)settle(item,{},true);if(!stopped()){auto result=clip(item.tag,stopped);for(auto n:item.aliases)results[n]=result;}};
    for(auto& item:work){if(item->batch&&!item->settled)batchable.push_back(item.get());else operations.push_back([&,item=item.get()]{fallback(*item);});}
    if(batchable.size()<4){for(auto* item:batchable){settle(*item,{},true);operations.push_back([&,item]{fallback(*item);});}}
    else {
        size_t groups=(batchable.size()+7)/8,position=0;
        for(size_t group=0;group<groups;group++){
            size_t count=batchable.size()/groups+(group<batchable.size()%groups);std::vector<Work*> items(batchable.begin()+position,batchable.begin()+position+count);position+=count;
            operations.push_back([&,items]{
                bool success=false;std::vector<std::shared_ptr<const AudioClip>> clips;
                try{
                    std::vector<const Bytes*> inputs;for(auto* item:items)inputs.push_back(&item->input);auto decoded=decodeBatch(ffmpeg_,workRoot_,inputs,stopped,decoderProcesses_);
                    if(decoded.success&&!stopped()){
                        success=true;for(auto& bytes:decoded.outputs){auto result=std::make_shared<AudioClip>();result->samples.resize(bytes.size()/sizeof(float));std::memcpy(result->samples.data(),bytes.data(),bytes.size());if(!std::all_of(result->samples.begin(),result->samples.end(),[](float value){return std::isfinite(value);})){success=false;break;}clips.push_back(std::move(result));}
                        success&=clips.size()==items.size();
                    }
                }catch(...){success=false;}
                if(success&&!stopped()){
                    std::vector<Work*> retry;
                    for(size_t n=0;n<items.size();n++){
                        auto& item=*items[n];bool rejected=false;if(item.patch){std::lock_guard lock(index_->rejected->mutex);rejected=index_->rejected->paths.contains(item.file);}
                        if(rejected||stopped()){settle(item,{},true);retry.push_back(&item);}else {++decoded_;settle(item,clips[n],false);}
                    }
                    for(auto* item:retry)fallback(*item);
                }else {
                    // None of a failed batch's possibly partial outputs can
                    // enter the cache. Release every claim before retrying any
                    // single clip, including patch fallback to another source.
                    for(auto* item:items)settle(*item,{},true);
                    for(auto* item:items)fallback(*item);
                }
            });
        }
    }
    std::atomic<size_t> next=0;std::vector<std::future<void>> jobs;
    for(size_t n=0;n<std::min<size_t>(4,operations.size());n++)jobs.push_back(std::async(std::launch::async,[&]{for(size_t k;(k=next++)<operations.size();){if(stopped())return;operations[k]();}}));
    for(auto& job:jobs)job.get();if(stopped())for(auto& result:results)result.reset();return results;
}
}
