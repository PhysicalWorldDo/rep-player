#include "resource_archive.hpp"
#include <fstream>
#include <iostream>

int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    auto mode=std::wstring_view(argv[1]);
    auto load=[&](const std::string& path)->std::optional<rep::ResourceArchiveEntry>{
        if(mode==L"load-error"&&path=="ogg")throw rep::Error("fixture unavailable");
        if(mode==L"empty"||(mode==L"skip"&&path=="ogg"))return {};
        if(path=="img")return rep::ResourceArchiveEntry{"sprite/test/real.img",{0,0x53,0xff,1}};
        if(path=="ogg")return rep::ResourceArchiveEntry{"sounds/test/effect.ogg",{'O','g','g','S',0,0xff}};
        return rep::ResourceArchiveEntry{"SPRITE\\test\\REAL.IMG",{1,2,3}};
    };
    std::vector<std::string> paths{"img","ogg"};
    if(mode==L"duplicate")paths.push_back("alias");
    size_t completed=0;
    try{
        auto count=rep::writeResourceArchive(argv[2],paths,load,
            [&](size_t done,size_t total){
                if(done!=completed+1||total!=paths.size())throw rep::Error("invalid progress");
                completed=done;
                if(mode==L"destination-race"&&done==total){
                    std::ofstream competing(std::filesystem::path(argv[2]),std::ios::binary);
                    competing<<"competing export";
                }
            },[&]{return mode==L"cancel"&&completed==1;});
        if(completed!=paths.size())throw rep::Error("missing progress");
        std::cout<<count<<'\n';
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
