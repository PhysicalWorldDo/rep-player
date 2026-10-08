#include "resources.hpp"
#include <fstream>
#include <iostream>

template<class T>
void snapshot(const std::filesystem::path& root,const std::string& logical){
    if constexpr(requires(T& assets){assets.index();}){
        T first(root);
        auto catalog=first.index();
        auto firstImg=first.preload(logical);
        auto firstFrame=first.frame(logical,0);
        {std::ofstream bad(root/L"!new-broken-package.npk",std::ios::binary);bad<<"broken fixture";}
        T second(catalog);
        auto secondImg=second.preload(logical);
        auto secondFrame=second.frame(logical,0);
        bool rebuiltRejects=false;
        try{T refreshed(root);}catch(const std::exception&){rebuiltRejects=true;}
        std::cout<<"{\"supported\":true,\"shared\":"<<(catalog==second.index()?"true":"false")
                 <<",\"private_decoded\":"<<(firstImg!=secondImg?"true":"false")
                 <<",\"same_root\":"<<(first.root()==second.root()?"true":"false")
                 <<",\"same_pixels\":"<<(firstFrame->cropped()==secondFrame->cropped()?"true":"false")
                 <<",\"new_index_rejects_bad_package\":"<<(rebuiltRejects?"true":"false")<<"}\n";
    }else{
        std::cout<<"{\"supported\":false}\n";
    }
}

int wmain(int argc,wchar_t** argv){try{
    if(argc<4)throw rep::Error("usage: npk_priority_resources_native frame|snapshot ROOT LOGICAL [FRAME]");
    const auto root=std::filesystem::path(argv[2]);
    const auto logical=rep::utf8(argv[3]);
    if(std::wstring_view(argv[1])==L"snapshot"){
        snapshot<rep::Assets>(root,logical);return 0;
    }
    rep::Assets assets(root);
    auto frame=assets.frame(logical,argc>4?std::stoi(argv[4]):0);
    const auto pixels=frame->cropped();
    std::cout<<"{\"pixel\":["<<unsigned(pixels.at(0))<<','<<unsigned(pixels.at(1))<<','
             <<unsigned(pixels.at(2))<<','<<unsigned(pixels.at(3))<<"],\"fallbacks\":"<<assets.fallbacks
             <<",\"actual_path\":\""<<frame->actualPath<<"\",\"actual_frame\":"<<frame->actualFrame<<"}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
