#include "movies.hpp"
#include <iostream>

static uint64_t files(const std::filesystem::path& cache){
    uint64_t count=0;for(auto& entry:std::filesystem::directory_iterator(cache))if(entry.is_regular_file()&&entry.path().filename()!=L"preserve.txt")count++;return count;
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=3)return 2;std::filesystem::path cache=argv[2];
    uint64_t shared=0,afterFirst=0,parallel=0,afterLast=0,afterReset=0,afterFailure=0,peak=0,afterCycles=0,afterExit=0;bool rewind=false,parallelReadable=false;
    {
        rep::Movies first(argv[1],cache),second(argv[1],cache);
        auto a=first.frame("wrapped.avi",1,0);auto crc=rep::crc(a->texture->rgba);
        first.frame("wrapped.avi",2,0);shared=files(cache);first.stop(1);afterFirst=files(cache);
        second.frame("wrapped.avi",3,0);parallel=files(cache);first.stop(2);afterLast=files(cache);
        second.frame("wrapped.avi",3,300);parallelReadable=rep::crc(second.frame("wrapped.avi",3,0)->texture->rgba)==crc;
        second.reset();afterReset=files(cache);
        for(int n=0;n<12;n++){first.frame("wrapped.avi",1,0);first.frame("wrapped.avi",1,300);rewind=rep::crc(first.frame("wrapped.avi",1,0)->texture->rgba)==crc;peak=std::max(peak,files(cache));first.reset();}
        afterCycles=files(cache);
        try{first.frame("invalid.avi",1,0);}catch(const rep::Error&){}afterFailure=files(cache);
        first.frame("plain.avi",1,0);first.reset();
        first.frame("wrapped.avi",1,0);
    }
    afterExit=files(cache);
    std::cout<<"{\"shared\":"<<shared<<",\"after_first_stop\":"<<afterFirst<<",\"parallel\":"<<parallel<<",\"after_last_stop\":"<<afterLast<<",\"after_reset\":"<<afterReset<<",\"peak\":"<<peak<<",\"after_cycles\":"<<afterCycles<<",\"after_failure\":"<<afterFailure<<",\"after_exit\":"<<afterExit<<",\"rewind\":"<<(rewind?"true":"false")<<",\"parallel_readable\":"<<(parallelReadable?"true":"false")<<"}";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
