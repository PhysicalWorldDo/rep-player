#include "export.hpp"
#include <iostream>

namespace {
std::string jsonString(const std::string& text){std::string out="\"";for(unsigned char c:text){switch(c){case '\\':out+="\\\\";break;case '"':out+="\\\"";break;case '\n':out+="\\n";break;case '\r':out+="\\r";break;case '\t':out+="\\t";break;default:out+=c;}}return out+'"';}
}
int wmain(int argc,wchar_t** argv){try{
    std::filesystem::path client,replay;rep::ExportOptions options;uint64_t cancelAfter=0,completed=0;
    for(int n=1;n<argc;n++){
        std::wstring_view key=argv[n];if(n+1>=argc)throw rep::Error("Missing export argument value");std::wstring value=argv[++n];
        if(key==L"--client")client=value;else if(key==L"--replay")replay=value;
        else if(key==L"--profile")options.protocol.profile=rep::clientProfile(value);
        else if(key==L"--codepage")options.protocol.codePage=rep::clientCodePage(value);
        else if(key==L"--output")options.outputDirectory=value;else if(key==L"--name")options.fileName=value;
        else if(key==L"--fps")options.fps=std::stoi(value);else if(key==L"--alpha")options.alpha=value==L"1";
        else if(key==L"--hide")options.hiddenImages.insert(rep::canonical(rep::utf8(value)));
        else if(key==L"--cancel-after")cancelAfter=std::stoull(value);
        else if(key==L"--format"){if(value==L"mov")options.format=rep::ExportFormat::Mov;else if(value==L"mp4")options.format=rep::ExportFormat::Mp4;else if(value==L"png")options.format=rep::ExportFormat::Png;else throw rep::Error("Unknown export format");}
        else throw rep::Error("Unknown export argument");
    }
    if(client.empty()||replay.empty())throw rep::Error("Usage: rep_export --client DIR --replay FILE --format mov|mp4|png --fps 30|60 --alpha 0|1 --output DIR --name NAME [--hide IMG]");
    wchar_t name[32768]{};GetModuleFileNameW(nullptr,name,32768);auto root=std::filesystem::path(name).parent_path().parent_path();
    rep::Gpu gpu(root/L"assets"/L"shaders");rep::Assets assets(client/L"ImagePacks2");
    auto result=rep::exportReplay(gpu,assets,client,root/L"runtime"/L"cache",replay,options,
        [&](const rep::ExportProgress& progress){completed=progress.completedFrames;},[&](){return cancelAfter&&completed>=cancelAfter;});
    std::cout<<"{\"output\":"<<jsonString(rep::utf8(result.outputPath.wstring()))<<",\"width\":"<<result.width<<",\"height\":"<<result.height
        <<",\"fps\":"<<result.fps<<",\"frames\":"<<result.frames<<",\"duration_ms\":"<<result.durationMilliseconds
        <<",\"executed_scenes\":"<<result.executedScenes<<",\"hidden_images\":"<<result.hiddenImageCount<<",\"alpha\":"<<(result.alpha?"true":"false")<<"}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
