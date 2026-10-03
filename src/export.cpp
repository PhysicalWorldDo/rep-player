#include "export.hpp"
#include "runtime_paths.hpp"
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <limits>

namespace rep {
namespace {
class Handle {
    HANDLE value_=nullptr;
public:
    Handle()=default;
    explicit Handle(HANDLE value):value_(value){}
    ~Handle(){reset();}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    HANDLE get()const{return value_;}
    void reset(HANDLE value=nullptr){if(value_&&value_!=INVALID_HANDLE_VALUE)CloseHandle(value_);value_=value;}
};
std::wstring quote(const std::wstring& value){
    std::wstring out=L"\"";size_t slashes=0;
    for(wchar_t c:value){if(c==L'\\'){slashes++;continue;}out.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;out+=c;}
    out.append(slashes*2,L'\\');out+=L'\"';return out;
}
bool within(const std::filesystem::path& root,const std::filesystem::path& child){
    auto base=std::filesystem::weakly_canonical(root),path=std::filesystem::weakly_canonical(child);
    auto a=base.begin(),b=path.begin();
    for(;a!=base.end();++a,++b){if(b==path.end())return false;auto x=a->wstring(),y=b->wstring();
        std::transform(x.begin(),x.end(),x.begin(),std::towlower);std::transform(y.begin(),y.end(),y.begin(),std::towlower);if(x!=y)return false;}
    return true;
}
std::string encoderError(const std::filesystem::path& log){
    std::ifstream input(log,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});
    if(bytes.size()>3000)bytes=bytes.substr(bytes.size()-3000);return bytes.empty()?"No encoder diagnostics":bytes;
}
class Encoder {
    Handle process_,writer_;
    std::filesystem::path log_;
    bool finished_=false;
public:
    Encoder(const std::filesystem::path& executable,const std::wstring& arguments,const std::filesystem::path& log):log_(log){
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE reader=nullptr,writer=nullptr;
        if(!CreatePipe(&reader,&writer,&security,0))throw Error("Cannot create export encoder pipe");
        Handle readHandle(reader);writer_.reset(writer);SetHandleInformation(writer_.get(),HANDLE_FLAG_INHERIT,0);
        Handle logHandle(CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
        Handle nullHandle(CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(logHandle.get()==INVALID_HANDLE_VALUE||nullHandle.get()==INVALID_HANDLE_VALUE)throw Error("Cannot create export encoder diagnostics");
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
        startup.hStdInput=readHandle.get();startup.hStdOutput=nullHandle.get();startup.hStdError=logHandle.get();PROCESS_INFORMATION process{};
        auto command=quote(executable.wstring())+arguments;
        if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,executable.parent_path().c_str(),&startup,&process))
            throw Error("Cannot start bundled FFmpeg export encoder");
        process_.reset(process.hProcess);CloseHandle(process.hThread);
    }
    ~Encoder(){if(!finished_&&process_.get()){TerminateProcess(process_.get(),1);WaitForSingleObject(process_.get(),2000);}}
    void frame(std::span<const uint8_t> bytes,const std::function<bool()>& cancelled){
        size_t offset=0;
        while(offset<bytes.size()){
            if(cancelled&&cancelled())throw Error("Export cancelled");
            DWORD count=0;DWORD size=DWORD(std::min<size_t>(bytes.size()-offset,1024*1024));
            if(!WriteFile(writer_.get(),bytes.data()+offset,size,&count,nullptr)||!count){
                writer_.reset();WaitForSingleObject(process_.get(),1000);throw Error("Export encoder rejected frame: "+encoderError(log_));}
            offset+=count;
        }
    }
    void finish(const std::function<bool()>& cancelled){
        writer_.reset();
        while(WaitForSingleObject(process_.get(),100)==WAIT_TIMEOUT)if(cancelled&&cancelled())throw Error("Export cancelled");
        DWORD code=1;if(!GetExitCodeProcess(process_.get(),&code))throw Error("Cannot read export encoder result");
        finished_=true;if(code)throw Error("Export encoding failed: "+encoderError(log_));
    }
};
}
ExportResult exportReplay(Gpu& gpu,Assets& assets,const std::filesystem::path& client,const std::filesystem::path& cache,
    const std::filesystem::path& replayPath,const ExportOptions& options,
    const std::function<void(const ExportProgress&)>& progress,const std::function<bool()>& cancelled){
    if(options.fps!=30&&options.fps!=60)throw Error("Export frame rate must be 30 or 60 fps");
    auto root=applicationDirectory();
    auto directory=options.outputDirectory.empty()?root/L"exports":options.outputDirectory;
    if(directory.is_relative())directory=root/directory;
    if(!within(root,directory))throw Error("Export directory must be inside rep_player");
    auto name=options.fileName.empty()?replayPath.stem().wstring():options.fileName;
    if(name.empty()||name==L"."||name==L".."||name.find_first_of(L"<>:\"/\\|?*")!=std::wstring::npos)
        throw Error("Export file name contains an invalid character");
    auto ext=std::filesystem::path(name).extension().wstring();
    std::transform(ext.begin(),ext.end(),ext.begin(),std::towlower);
    if(ext==L".mov"||ext==L".mp4"||ext==L".png")name=std::filesystem::path(name).stem().wstring();
    const bool png=options.format==ExportFormat::Png;
    const auto suffix=png?std::wstring{}:options.format==ExportFormat::Mov?std::wstring(L".mov"):std::wstring(L".mp4");
    auto output=directory/(name+suffix),staging=directory/(name+L".partial"+suffix);
    if(std::filesystem::exists(output))throw Error("Export destination already exists; choose another file name");
    if(std::filesystem::exists(staging))throw Error("Partial export already exists; choose another file name");
    auto ffmpeg=ffmpegExecutable();if(!std::filesystem::is_regular_file(ffmpeg))throw Error("Bundled FFmpeg export encoder is missing");
    auto checkCancelled=[&](){if(cancelled&&cancelled())throw Error("Export cancelled");};
    checkCancelled();ExportProgress update;update.stage=L"Preparing";if(progress)progress(update);
    Replay replay(replayPath,clientReplayOptions(client,options.protocol));Scene scene;int32_t duration=0;uint64_t sceneCount=0;
    while(replay.next(scene)){duration=std::max(duration,scene.timestamp);sceneCount++;checkCancelled();}
    if(!sceneCount)throw Error("REP has no scenes to export");replay.rewind();
    ExportResult result;result.outputPath=output;result.width=replay.header.width();result.height=replay.header.height();
    result.fps=options.fps;result.durationMilliseconds=duration;result.frames=(uint64_t(duration)*options.fps+999)/1000+1;
    result.alpha=options.alpha&&options.format!=ExportFormat::Mp4;result.hiddenImageCount=options.hiddenImages.size();
    update.totalFrames=result.frames;if(progress)progress(update);
    Executor executor(gpu,assets,cache,client);executor.setTransparent(result.alpha);executor.setHiddenImages(options.hiddenImages);
    executor.attach(replay);executor.prepare(cancelled);checkCancelled();
    std::filesystem::create_directories(directory);if(png)std::filesystem::create_directory(staging);
    auto target=png?staging/L"frame_%06d.png":staging;
    std::wstring arguments=L" -hide_banner -loglevel error -nostdin -n -f rawvideo -pixel_format rgba -video_size "+
        std::to_wstring(result.width)+L"x"+std::to_wstring(result.height)+L" -framerate "+std::to_wstring(options.fps)+L" -i pipe:0 -an -threads 4 ";
    switch(options.format){
    case ExportFormat::Mov:arguments+=L"-c:v prores_ks -profile:v 4 -pix_fmt ";arguments+=result.alpha?L"yuva444p10le -alpha_bits 16 ":L"yuv444p10le ";arguments+=L"-f mov ";break;
    case ExportFormat::Mp4:arguments+=L"-c:v libx264 -preset medium -crf 18 -pix_fmt ";arguments+=(result.width%2||result.height%2)?L"yuv444p ":L"yuv420p ";arguments+=L"-movflags +faststart -f mp4 ";break;
    case ExportFormat::Png:arguments+=L"-c:v png -pix_fmt rgba -start_number 0 -f image2 ";break;
    }
    arguments+=quote(target.wstring());Encoder encoder(ffmpeg,arguments,directory/(name+L".ffmpeg.log"));
    bool hasScene=false,ended=false;update.stage=L"Rendering";
    for(uint64_t frame=0;frame<result.frames;frame++){
        checkCancelled();int64_t sample=int64_t(frame*1000/options.fps);
        while(!ended&&(!hasScene||scene.timestamp<sample||frame+1==result.frames)){
            if(!replay.next(scene)){ended=true;break;}hasScene=true;executor.execute(scene);checkCancelled();
        }
        auto rgba=gpu.screenshot(executor.output(),result.alpha);encoder.frame(rgba,cancelled);
        update.completedFrames=frame+1;update.sampledMilliseconds=int32_t(std::min<int64_t>(sample,std::numeric_limits<int32_t>::max()));
        if(progress)progress(update);
    }
    update.stage=L"Encoding";if(progress)progress(update);encoder.finish(cancelled);checkCancelled();
    std::filesystem::rename(staging,output);result.executedScenes=executor.statistics.scenes;
    update.stage=L"Complete";if(progress)progress(update);return result;
}
}
