#include "resources.hpp"
#include <iostream>

int wmain(int argc,wchar_t** argv){try{
    if(argc<6)throw rep::Error("usage: palette_paths ROOT PATCHES FRAME LOGICAL...");
    rep::Assets assets(argv[1],std::stoi(argv[2])!=0);
    const int index=std::stoi(argv[3]);
    // argv[4] selects independent or shared-index readers for revision checks.
    rep::Assets shared(assets.index());
    const bool sequence=argc>6;
    if(sequence)std::cout<<"{\"samples\":[";
    for(int k=5;k<argc;k++){
        auto& reader=std::wstring_view(argv[4])==L"shared"&&k>5?shared:assets;
        auto frame=reader.frame(rep::utf8(argv[k]),index);
        auto pixels=frame->cropped();
        if(k>5)std::cout<<',';
        std::cout<<"{\"pixel\":["<<unsigned(pixels.at(0))<<','<<unsigned(pixels.at(1))<<','
                 <<unsigned(pixels.at(2))<<','<<unsigned(pixels.at(3))<<"],\"fallbacks\":"<<reader.fallbacks
                 <<",\"actual_path\":\""<<frame->actualPath<<"\",\"actual_frame\":"<<frame->actualFrame
                 <<",\"metadata\":["<<frame->x<<','<<frame->y<<','<<frame->width<<','<<frame->height<<','<<frame->fullWidth<<','<<frame->fullHeight<<']'
                 <<",\"geometry_scale\":"<<frame->geometryScale
                 <<",\"revision\":"<<reader.sourceRevision()<<"}";
    }
    if(sequence)std::cout<<"]}";
    std::cout<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
