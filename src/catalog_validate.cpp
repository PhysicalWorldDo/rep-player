#include "catalog.hpp"
#include <windows.h>
#include <iostream>

static std::string json(std::string value){std::string out="\"";for(unsigned char c:value){if(c=='"'||c=='\\')out+='\\';out+=c;}return out+'"';}
int wmain(int argc,wchar_t** argv){try{
    if(argc<2)throw rep::Error("client path required");wchar_t module[32768];GetModuleFileNameW(nullptr,module,32768);auto root=std::filesystem::path(module).parent_path().parent_path();
    rep::ui::Catalog catalog(root/L"ui_design"/L"data"/L"skill-name-map.json");catalog.scan(std::filesystem::path(argv[1])/L"Replay"/L"SkillReplay");auto query=argc>2?rep::utf8(argv[2]):std::string{};
    std::cout<<"{\"total\":"<<catalog.items().size()<<",\"matched\":"<<catalog.matchedCount()<<",\"items\":[";bool first=true;
    for(const auto& item:catalog.items())if(catalog.matches(item,query)){if(!first)std::cout<<',';first=false;std::cout<<"{\"file\":"<<json(rep::utf8(item.path.filename().wstring()))<<",\"path\":"<<json(rep::utf8(item.path.wstring()))<<",\"zh\":"<<json(item.displayZh)<<",\"en\":"<<json(item.displayEn)<<"}";}
    std::cout<<"]}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
