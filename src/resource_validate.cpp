#include "resources.hpp"
#include <iostream>
int wmain(int argc,wchar_t** argv){try{
    if(argc<4)throw rep::Error("usage: rep_resources --img file index [palette] | --npk root logical index");
    std::shared_ptr<rep::Frame> f;
    if(std::wstring_view(argv[1])==L"--img"){rep::Img img(rep::readFile(argv[2]));f=img.frame(std::stoi(argv[3]),argc>4?std::stoi(argv[4]):0);}
    else {rep::Assets assets(argv[2]);f=assets.frame(rep::utf8(argv[3]),std::stoi(argv[4]));}
    auto rgba=f->cropped();std::cout<<"{\"metadata\":["<<f->x<<','<<f->y<<','<<f->width<<','<<f->height<<','<<f->fullWidth<<','<<f->fullHeight<<"],\"effective_texture\":["<<f->texture->width<<','<<f->texture->height<<"],\"logical_texture\":["<<f->texture->logicalWidth<<','<<f->texture->logicalHeight<<"],\"rgba_crc32\":"<<rep::crc(rgba)<<",\"native_empty\":"<<(f->empty?"true":"false")<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
