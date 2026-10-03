#include "catalog.hpp"
#include <windows.h>
#include <iostream>

static std::string json(std::string value){std::string out="\"";for(unsigned char c:value){if(c=='"'||c=='\\')out+='\\';out+=c;}return out+'"';}
int wmain(int argc,wchar_t** argv){try{
    if(argc<2)throw rep::Error("client path required");wchar_t module[32768];GetModuleFileNameW(nullptr,module,32768);auto root=std::filesystem::path(module).parent_path().parent_path();
    bool scanRoot=std::wstring_view(argv[1])==L"--scan-root";if(scanRoot&&argc<3)throw rep::Error("scan root required");int queryIndex=scanRoot?3:2;
    auto replayRoot=scanRoot?std::filesystem::path(argv[2]):std::filesystem::path(argv[1])/L"Replay";
    rep::ui::Catalog catalog(root/L"ui_design"/L"data"/L"skill-name-map.json");catalog.scan(replayRoot);auto query=argc>queryIndex?rep::utf8(argv[queryIndex]):std::string{};
    std::cout<<"{\"total\":"<<catalog.items().size()<<",\"matched\":"<<catalog.matchedCount()<<",\"items\":[";bool first=true;
    for(const auto& item:catalog.items())if(catalog.matches(item,query)){if(!first)std::cout<<',';first=false;std::cout<<"{\"file\":"<<json(rep::utf8(item.path.filename().wstring()))<<",\"path\":"<<json(rep::utf8(item.path.wstring()))<<",\"relative_path\":"<<json(item.relativePath)<<",\"job\":"<<json(item.job)<<",\"job_zh\":"<<json(item.jobZh)<<",\"directories\":[";
        bool firstDirectory=true;for(const auto& directory:item.directories){if(!firstDirectory)std::cout<<',';firstDirectory=false;std::cout<<json(directory);}std::cout<<"],\"zh\":"<<json(item.displayZh)<<",\"en\":"<<json(item.displayEn)<<"}";}
    std::cout<<"]}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
