#include "movies.hpp"
#include <chrono>
#include <iostream>

static uint32_t checksum(const std::shared_ptr<rep::Frame>& frame){
    if(!frame||!frame->texture)throw rep::Error("missing movie frame");
    return rep::crc(frame->texture->rgba);
}
template<class T>static void transaction(T& movies,const std::string& first,const std::string& second){
    if constexpr(requires(T& m){m.snapshot();m.beginTransaction();m.commitTransaction();m.rollbackTransaction();}){
        auto original=movies.frame(first,1,0);auto originalCrc=checksum(original);auto before=movies.snapshot();
        auto originalColor=movies.color(1,91,false);
        movies.beginTransaction();
        auto candidate=movies.frame(second,1,700);auto candidateCrc=checksum(candidate);
        movies.stop(1);movies.frame(second,2,0);movies.rollbackTransaction();
        bool rollback=movies.info(1).width==original->width&&!movies.info(2).width&&movies.color(1,91,false)==originalColor;
        bool immutable=checksum(original)==originalCrc&&checksum(movies.frame("",1,0))==originalCrc;
        movies.beginTransaction();movies.frame(second,1,700);movies.commitTransaction();
        bool committed=checksum(movies.frame("",1,700))==candidateCrc;
        movies.beginTransaction();auto candidateState=movies.snapshot();movies.restoreSnapshot(before);
        bool restored=checksum(movies.frame("",1,0))==originalCrc&&movies.color(1,91,false)==originalColor;
        movies.restoreSnapshot(candidateState);bool nested=checksum(movies.frame("",1,700))==candidateCrc;
        movies.rollbackTransaction();
        movies.beginTransaction();bool exception=false;
        try{movies.frame("invalid.avi",1,0);}catch(const rep::Error&){exception=true;}
        movies.rollbackTransaction();bool failedRollback=exception&&checksum(movies.frame("",1,700))==candidateCrc;
        std::cout<<"{\"supported\":true,\"rollback\":"<<rollback<<",\"immutable\":"<<immutable
                 <<",\"committed\":"<<committed<<",\"snapshot\":"<<restored<<",\"nested_restore\":"<<nested
                 <<",\"exception_rollback\":"<<failedRollback<<",\"original_color\":"<<originalColor<<'}';
    }else std::cout<<"{\"supported\":false}";
}
template<class T>static void snapshotLifetime(T& movies,const std::string& logical,const std::filesystem::path& cache){
    if constexpr(requires(T& m){m.snapshot();}){
        auto first=movies.frame(logical,1,0);auto expected=checksum(first);auto snapshot=movies.snapshot();movies.stop(1);
        size_t retained=0;for(auto& entry:std::filesystem::directory_iterator(cache))if(entry.is_regular_file())retained++;
        movies.restoreSnapshot(snapshot);bool info=movies.info(1).frames>0;bool decoded=checksum(movies.frame("",1,0))==expected;
        movies.stop(1);size_t after=0;for(auto& entry:std::filesystem::directory_iterator(cache))if(entry.is_regular_file())after++;
        std::cout<<"{\"retained\":"<<retained<<",\"after_restore_stop\":"<<after<<",\"info\":"<<info<<",\"decoded\":"<<decoded<<'}';
    }else std::cout<<"{\"retained\":99}";
}
int wmain(int argc,wchar_t** argv){try{
    if(argc<6)return 2;
    std::string mode=rep::utf8(argv[1]),first=rep::utf8(argv[3]);rep::Movies movies(argv[2],argv[4]);
    if(mode=="transaction"){transaction(movies,first,rep::utf8(argv[5]));return 0;}
    if(mode=="lifetime"){snapshotLifetime(movies,first,argv[4]);return 0;}
    if(mode=="binding"){
        auto original=movies.frame(first,1,0);auto crc=checksum(original);movies.frame(first,1,700);
        bool immutable=checksum(original)==crc;
        auto changed=movies.frame(rep::utf8(argv[5]),1,0);auto fresh=movies.frame(rep::utf8(argv[5]),2,0);
        std::cout<<"{\"immutable\":"<<immutable<<",\"rebound\":"<<(checksum(changed)==checksum(fresh))
                 <<",\"width\":"<<movies.info(1).width<<'}';return 0;
    }
    std::cout<<'[';
    for(int n=5;n<argc;n++){
        auto start=std::chrono::steady_clock::now();auto frame=movies.frame(first,1,uint32_t(std::stoul(argv[n])));
        auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        uint64_t transparent=0,partial=0;for(size_t p=3;p<frame->texture->rgba.size();p+=4){auto a=frame->texture->rgba[p];transparent+=a==0;partial+=a>0&&a<255;}
        if(n>5)std::cout<<',';
        std::cout<<"{\"crc\":"<<checksum(frame)<<",\"milliseconds\":"<<elapsed<<",\"transparent\":"<<transparent<<",\"partial\":"<<partial<<'}';
    }
    std::cout<<']';return 0;
}catch(const std::exception& error){std::cerr<<error.what();return 1;}}
