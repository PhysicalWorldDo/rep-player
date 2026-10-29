#include "audio.hpp"
#include <atomic>
#include <concepts>
#include <iostream>
#include <thread>

using namespace rep;
template<class Track> bool seekCancellable(Track& track,int64_t target,const std::function<bool()>& cancelled){
    if constexpr(requires { {track.seek(target,cancelled)} -> std::same_as<bool>; })return track.seek(target,cancelled);
    else {track.seek(target);return true;}
}
template<class Player> uint64_t seekCancellable(Player& player,int64_t target,bool playing,const std::function<bool()>& cancelled){
    if constexpr(requires {player.seek(target,playing,cancelled);})return player.seek(target,playing,cancelled);
    else return player.seek(target,playing);
}
template<class Player> constexpr bool cancellablePlayer=requires(Player& p,const std::function<bool()>& c){p.seek(1,false,c);};
static void require(bool value,const char* message){if(!value)throw Error(message);}
template<class Predicate> static void waitFor(Predicate predicate){
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!predicate()){if(std::chrono::steady_clock::now()>=end)throw Error("audio cancellation wait timed out");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=4)return 2;
    Replay replay(argv[3]);auto track=std::make_unique<AudioTrack>(argv[2],std::filesystem::path(argv[2])/L"cache",replay);
    if(std::wstring_view(argv[1])==L"track"){
        size_t polls=0;bool completed=seekCancellable(*track,1000,[&]{return ++polls>=20;});
        require(!completed,"obsolete audio seek consumed the complete event history");
        require(polls>=20,"audio seek did not poll its navigation cancellation");
        require(seekCancellable(*track,37,{}),"next audio seek could not recover after cancellation");
        require(track->positionMilliseconds()==37,"recovered audio seek used a partial old position");
        std::cout<<"{\"cancelled\":true,\"recovered\":true,\"polls\":"<<polls<<"}\n";return 0;
    }
    AudioPlayer player(std::move(track));player.volume(0,true);require(player.available(),"test audio device unavailable");
    if(std::wstring_view(argv[1])==L"player-cancel"){
        std::atomic<bool> polled=false;
        auto serial=seekCancellable(player,1000,true,[&]{polled=true;return true;});
        waitFor([&]{return polled.load();});std::this_thread::sleep_for(std::chrono::milliseconds(40));
        require(player.appliedSeekSerial()<serial&&!player.resumePrepared(serial)&&player.positionMilliseconds()==0,
            "cancelled audio request started mixing a partial interpreter state");
        auto next=player.seek(37,false);waitFor([&]{return player.appliedSeekSerial()>=next;});
        require(player.positionMilliseconds()==37&&player.available(),"cancelled playing seek did not recover");
        std::cout<<"{\"cancelled_stays_silent\":true}\n";return 0;
    }
    std::atomic<bool> entered=false,released=false;
    auto old=seekCancellable(player,1000,false,[&]{entered=true;while(!released.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));return false;});
    // A missing callback API must fail as a behavior assertion, not hang.
    if constexpr(!cancellablePlayer<AudioPlayer>)released=true;
    else waitFor([&]{return entered.load();});
    auto latest=player.seek(37,false);released=true;
    waitFor([&]{return player.appliedSeekSerial()>=latest;});
    require(entered,"audio worker ignored the navigation cancellation callback");
    require(player.positionMilliseconds()==37&&player.available()&&player.error().empty(),"superseded audio seek poisoned the next request");
    require(!player.resumePrepared(old),"obsolete audio serial resumed");
    waitFor([&]{return player.pcmReadySerial()>=latest;});
    require(player.resumePrepared(latest),"latest prepared audio serial did not resume");
    player.pause();
    std::cout<<"{\"superseded\":true,\"latest\":"<<latest<<",\"available\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
