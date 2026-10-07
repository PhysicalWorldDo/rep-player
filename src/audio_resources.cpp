#include "audio_resources.hpp"
#include "runtime_paths.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
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
    if(name=="UNINTERRUPTED_EFFECT")return AudioKind::UninterruptedEffect;if(name=="RANDOM"||name=="GROUP"||name=="PLAYTAG")return AudioKind::Composite;return AudioKind::Unknown;
}
double number(const Attributes& values,const std::string& key,double fallback){
    auto it=values.find(key);if(it==values.end())return fallback;char* tail=nullptr;auto value=std::strtod(it->second.c_str(),&tail);
    return tail!=it->second.c_str()&&!*tail&&std::isfinite(value)?value:fallback;
}
struct Handle {
    HANDLE value=nullptr;
    Handle()=default;explicit Handle(HANDLE handle):value(handle){}
    ~Handle(){close();}Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    void close(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=nullptr;}
};
struct Decoded {Bytes data;std::string error;bool cancelled=false;};
Decoded decode(const std::filesystem::path& ffmpeg,const Bytes& input,const std::function<bool()>& cancelled){
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
    DeleteProcThreadAttributeList(startup.lpAttributeList);if(!started)throw Error("cannot start bundled audio decoder");Handle process(information.hProcess),thread(information.hThread);thread.close();inRead.close();outWrite.close();errWrite.close();
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
}
AudioResources::AudioResources(std::filesystem::path clientRoot,std::filesystem::path cacheRoot):client_(std::move(clientRoot)),ffmpeg_(ffmpegExecutable()){
    // PCM is memory-owned; existing on-disk caches and source assets are untouched.
    (void)cacheRoot;loadDefinitions();
}
void AudioResources::report(const std::string& key,std::string message,bool missing){
    std::lock_guard lock(mutex_);if(reported_.insert(key).second){diagnostics_.push_back(std::move(message));if(missing)missing_++;}
}
void AudioResources::loadDefinitions(){
    auto path=client_/L"audio.xml";if(!std::filesystem::is_regular_file(path)){report("registry","Client audio.xml is unavailable",true);return;}
    try{
        auto bytes=readFile(path);std::string_view xml(reinterpret_cast<const char*>(bytes.data()),bytes.size());size_t position=0;
        while((position=xml.find('<',position))!=std::string_view::npos){
            if(xml.substr(position,4)=="<!--"){auto end=xml.find("-->",position+4);position=end==std::string_view::npos?xml.size():end+3;continue;}
            if(xml.substr(position,9)=="<![CDATA["){auto end=xml.find("]]>",position+9);position=end==std::string_view::npos?xml.size():end+3;continue;}
            size_t start=++position;if(position==xml.size())break;if(xml[position]=='/'||xml[position]=='?'||xml[position]=='!'){auto end=xml.find('>',position);position=end==std::string_view::npos?xml.size():end+1;continue;}
            while(position<xml.size()&&!whitespace(xml[position])&&xml[position]!='/'&&xml[position]!='>')position++;auto type=kind(xml.substr(start,position-start));auto attributeStart=position;
            char quote=0;while(position<xml.size()){char c=xml[position];if(quote){if(c==quote)quote=0;}else if(c=='\''||c=='\"')quote=c;else if(c=='>')break;position++;}
            auto values=attributes(xml.substr(attributeStart,position-attributeStart),0);if(position<xml.size())position++;
            if(type==AudioKind::Unknown||!values.contains("ID"))continue;SoundDefinition definition;definition.tag=values["ID"];definition.kind=type;definition.file=values["FILE"];
            definition.playable=type!=AudioKind::Composite&&!definition.file.empty();definition.loopDelay=number(values,"LOOP_DELAY",-1);definition.loopDelayRange=number(values,"LOOP_DELAY_RANGE",0);
            definition.duplicateLimit=int(std::clamp(number(values,"DUPLICATE_LIMIT",0),0.,double(INT_MAX)));definition.duplicatePolicy=values["DUPLICATE_POLICY"];definition.volumeAdjust=values["VOLUME_ADJUST"];
            definition.fadeData=values["FADE_DATA"];definition.sidechainData=values["SIDECHAIN_DATA"];definition.ignore3dSound=values["IGNORE_3DSOUND"]=="TRUE";
            definitions_.try_emplace(definition.tag,std::move(definition));
        }
    }catch(const std::exception& error){report("registry","Cannot read audio.xml: "+std::string(error.what()),true);}
}
const SoundDefinition* AudioResources::definition(const std::string& tag)const{
    auto it=definitions_.find(tag);return it==definitions_.end()?nullptr:&it->second;
}
std::vector<std::string> AudioResources::diagnostics()const{std::lock_guard lock(mutex_);return diagnostics_;}
void AudioResources::indexPackage(const std::filesystem::path& package){
    auto packageKey=canonical(utf8(package.filename().wstring()));if(indexedPackages_.contains(packageKey))return;indexedPackages_.insert(packageKey);
    std::ifstream file(package,std::ios::binary);if(!file)return;std::array<uint8_t,20> header{};file.read(reinterpret_cast<char*>(header.data()),header.size());
    if(!file||std::memcmp(header.data(),"NeoplePack_Bill\0",16))throw Error("invalid sound NPK header: "+packageKey);
    uint32_t count=at<uint32_t>(header,16);file.seekg(0,std::ios::end);auto size=file.tellg();if(size<20||uint64_t(count)*264>uint64_t(size)-20)throw Error("truncated sound NPK table: "+packageKey);
    file.seekg(20);Bytes table(size_t(count)*264);file.read(reinterpret_cast<char*>(table.data()),table.size());if(!file)throw Error("cannot read sound NPK table: "+packageKey);
    std::string seed="puchikon@neople dungeon and fighter ";while(seed.size()<255)seed+="DNF";seed.resize(255);seed.push_back(0);
    for(uint32_t i=0;i<count;i++){
        auto record=std::span(table).subspan(size_t(i)*264,264);std::string logical;for(size_t j=0;j<256;j++){char c=char(record[8+j]^uint8_t(seed[j]));if(!c)break;logical+=c;}
        auto offset=at<uint32_t>(record,0),length=at<uint32_t>(record,4);if(uint64_t(offset)+length>uint64_t(size))throw Error("sound NPK entry exceeds package: "+packageKey);
        entries_.try_emplace(logicalPath(std::move(logical)),Entry{package,offset,length});
    }
}
std::optional<AudioResources::Entry> AudioResources::resolve(const std::string& logical){
    std::lock_guard lock(mutex_);if(auto it=entries_.find(logical);it!=entries_.end())return it->second;
    auto directory=client_/L"SoundPacks";auto parent=logical.substr(0,logical.find_last_of('/'));
    while(parent.find('/')!=std::string::npos){auto name=parent;std::replace(name.begin(),name.end(),'/','_');auto package=directory/wide(name+".npk");if(std::filesystem::is_regular_file(package))indexPackage(package);
        if(auto it=entries_.find(logical);it!=entries_.end())return it->second;parent=parent.substr(0,parent.find_last_of('/'));
    }
    if(!allPackagesIndexed_){
        allPackagesIndexed_=true;std::vector<std::filesystem::path> packages;if(std::filesystem::is_directory(directory))for(auto& item:std::filesystem::directory_iterator(directory))if(item.is_regular_file()&&canonical(utf8(item.path().extension().wstring()))==".npk")packages.push_back(item.path());
        std::sort(packages.begin(),packages.end());for(auto& package:packages)indexPackage(package);
    }
    auto it=entries_.find(logical);return it==entries_.end()?std::nullopt:std::optional<Entry>(it->second);
}
std::shared_ptr<const AudioClip> AudioResources::clip(const std::string& tag,const std::function<bool()>& cancelled){
    if(cancelled&&cancelled())return {};auto source=definition(tag);if(!source){report("tag:"+tag,"Unknown audio TAG: "+tag,true);return {};}if(!source->playable)return {};
    auto file=logicalPath(source->file);std::shared_ptr<Pending> pending;bool owner=false;
    {std::lock_guard lock(mutex_);if(auto it=clips_.find(file);it!=clips_.end())return it->second;if(unavailable_.contains(file))return {};
        auto [it,inserted]=pending_.try_emplace(file,std::make_shared<Pending>());pending=it->second;owner=inserted;}
    if(!owner){std::unique_lock lock(pending->mutex);while(!pending->done){if(cancelled&&cancelled())return {};pending->ready.wait_for(lock,std::chrono::milliseconds(25));}return pending->result;}
    std::shared_ptr<const AudioClip> result;bool wasCancelled=false;
    try{
        Bytes input;if(file.rfind("sounds/",0)==0){auto entry=resolve(file);if(!entry)throw Error("sound resource is absent from SoundPacks: "+source->file);
            std::ifstream stream(entry->package,std::ios::binary);stream.seekg(entry->offset);input.resize(entry->length);stream.read(reinterpret_cast<char*>(input.data()),input.size());if(!stream)throw Error("cannot read sound NPK payload");
        }else{auto path=client_/wide(source->file);if(!std::filesystem::is_regular_file(path))throw Error("audio file is absent: "+source->file);input=readFile(path);}
        auto decoded=decode(ffmpeg_,input,cancelled);wasCancelled=decoded.cancelled;if(!wasCancelled){if(decoded.data.empty())throw Error("audio decode failed: "+decoded.error);
            auto clip=std::make_shared<AudioClip>();clip->samples.resize(decoded.data.size()/sizeof(float));std::memcpy(clip->samples.data(),decoded.data.data(),decoded.data.size());
            if(!std::all_of(clip->samples.begin(),clip->samples.end(),[](float sample){return std::isfinite(sample);}))throw Error("audio decoder returned non-finite PCM");result=std::move(clip);decoded_++;
        }
    }catch(const std::exception& error){wasCancelled=cancelled&&cancelled();if(!wasCancelled)report("file:"+file,"Audio TAG "+tag+": "+error.what(),true);}
    {std::lock_guard lock(mutex_);if(result)clips_[file]=result;else if(!wasCancelled)unavailable_.insert(file);pending_.erase(file);}
    {std::lock_guard lock(pending->mutex);pending->result=result;pending->done=true;}pending->ready.notify_all();return result;
}
}
