#include "movies.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>

static void expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static uint32_t checksum(const std::shared_ptr<rep::Frame>& frame){expect(frame&&frame->texture,"missing movie frame");return rep::crc(frame->texture->rgba);}
template<class T>static void hooks(T& movies,const std::string& source,bool bink){
    if constexpr(requires(T& m){m.setDemandWaitCallback(std::function<void()>{});}){
        unsigned calls=0;bool wrongThread=false;auto owner=std::this_thread::get_id();
        movies.setDemandWaitCallback([&]{calls++;wrongThread|=std::this_thread::get_id()!=owner;});
        std::vector<uint32_t> positions=bink?std::vector<uint32_t>{1,1,2,32,33,3}:std::vector<uint32_t>{0,0,34,34,3000,3034,34};
        std::vector<unsigned> expected=bink?std::vector<unsigned>{1,0,0,1,0,1}:std::vector<unsigned>{1,0,0,0,1,0,1};
        std::shared_ptr<rep::Frame> first;uint32_t original=0;std::vector<unsigned> counts;std::vector<uint32_t> values;
        for(size_t n=0;n<positions.size();n++){
            calls=0;auto frame=movies.frame(source,1,positions[n]);counts.push_back(calls);values.push_back(checksum(frame));
            expect(calls==expected[n],"movie wait callback is missing, duplicated, or called on a ready/adjacent frame");
            if(n==0){first=frame;original=checksum(first);}expect(checksum(first)==original,"demand hook changed previously returned pixels");
        }
        calls=0;expect(checksum(movies.frame(source,2,positions.back()))==values.back(),"shared decoder binding changed pixels");expect(calls==0&&!wrongThread,"ready shared binding notified wait or called another thread");
        std::cout<<"{\"passed\":true,\"counts\":[";for(size_t n=0;n<counts.size();n++){if(n)std::cout<<',';std::cout<<counts[n];}std::cout<<"],\"crc\":[";for(size_t n=0;n<values.size();n++){if(n)std::cout<<',';std::cout<<values[n];}std::cout<<"]}";
    }else throw std::runtime_error("Movies demand-wait callback API is missing");
}
template<class T>static void cancellation(T& movies,const std::string& first,const std::string& second){
    if constexpr(requires(T& m){m.setDemandWaitCallback(std::function<void()>{});}){
        auto frame=movies.frame(first,1,0);auto original=checksum(frame);movies.beginTransaction();unsigned calls=0;
        movies.setDemandWaitCallback([&]{calls++;throw rep::Error("cancel before expensive movie preparation");});
        bool cancelled=false;try{movies.frame(second,1,0);}catch(const rep::Error&){cancelled=true;}
        movies.setDemandWaitCallback({});movies.rollbackTransaction();
        expect(cancelled&&calls==1,"movie preparation did not stop at the demand gate");expect(checksum(frame)==original&&checksum(movies.frame("",1,0))==original,"cancelled movie wait changed live transaction/pixels");
        std::cout<<"{\"passed\":true,\"callbacks\":"<<calls<<'}';
    }else throw std::runtime_error("Movies demand-wait callback API is missing");
}
int wmain(int argc,wchar_t** argv){try{
    expect(argc>=5,"mode client source cache [second] required");std::string mode=rep::utf8(argv[1]);rep::Movies movies(argv[2],argv[4]);
    if(mode=="cancel"){expect(argc==6,"second source required");cancellation(movies,rep::utf8(argv[3]),rep::utf8(argv[5]));}
    else hooks(movies,rep::utf8(argv[3]),mode=="bink");return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
