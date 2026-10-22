#include "movies.hpp"
#include <iostream>

int wmain(int argc,wchar_t** argv){try{
    if(argc<5)return 2;
    rep::Movies movies(argv[1],argv[3]);
    std::cout<<'[';
    for(int n=4;n<argc;n++){
        auto frame=movies.frame(rep::utf8(argv[2]),1,uint32_t(std::stoul(argv[n])));
        if(!frame)throw rep::Error("movie missing");
        const auto& rgba=frame->texture->rgba;rep::Bytes rgb;rgb.reserve(rgba.size()/4*3);
        uint64_t transparent=0,partial=0,opaque=0;
        for(size_t p=0;p<rgba.size();p+=4){
            rgb.insert(rgb.end(),rgba.begin()+p,rgba.begin()+p+3);
            auto alpha=rgba[p+3];transparent+=alpha==0;partial+=alpha>0&&alpha<255;opaque+=alpha==255;
        }
        if(n>4)std::cout<<',';
        std::cout<<"{\"rgba_crc32\":"<<rep::crc(rgba)<<",\"rgb_crc32\":"<<rep::crc(rgb)
                 <<",\"transparent\":"<<transparent<<",\"partial\":"<<partial<<",\"opaque\":"<<opaque<<'}';
    }
    std::cout<<"]\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
