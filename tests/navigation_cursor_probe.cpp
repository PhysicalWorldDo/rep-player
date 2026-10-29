#include "protocol.hpp"
#include <iostream>
using namespace rep;
template<class T> concept NavigationCursor = requires(T& a,T& b,Scene& row){
    a.swapCursor(b); a.next(row,std::function<bool()>{});
};
template<class T> void verify(const std::filesystem::path& path){
    if constexpr(!NavigationCursor<T>)std::cout<<"{\"implemented\":false}\n";
    else{
        T live(path),candidate(live);Scene row;live.next(row);candidate.next(row);candidate.next(row);
        bool cancelled=false;T discarded(live);int checks=0;
        cancelled=!discarded.next(row,[&]{return ++checks>=2;});
        live.next(row);bool unchanged=row.ordinal==1;
        live.swapCursor(candidate);live.next(row);bool adopted=row.ordinal==2;
        std::cout<<"{\"implemented\":true,\"cancelled\":"<<(cancelled?"true":"false")
            <<",\"unchanged\":"<<(unchanged?"true":"false")<<",\"adopted\":"<<(adopted?"true":"false")<<"}\n";
    }
}
int wmain(int argc,wchar_t** argv){if(argc!=2)return 2;try{verify<Replay>(argv[1]);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
