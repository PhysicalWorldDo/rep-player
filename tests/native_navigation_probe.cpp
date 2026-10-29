#include "engine.hpp"
#include <iostream>

using namespace rep;
int wmain(int argc,wchar_t** argv){
    if(argc!=4)return 2;
    try{
        std::filesystem::path root=argv[1],client=argv[2],path=argv[3];
        Gpu gpu(root/L"assets"/L"shaders");Assets assets(client/L"ImagePacks2");
        Replay replay(path);Executor executor(gpu,assets,root/L"runtime"/L"cache",client);
        executor.attach(replay);Playback playback;playback.attach(replay,executor);playback.start();playback.select(0);
        Replay reference(path);Executor expected(gpu,assets,root/L"runtime"/L"reference",client);expected.attach(reference);
        Scene row,first,target;reference.next(first);expected.execute(first);
        while(reference.next(row)){if(row.ordinal==4){target=row;break;}}
        expected.execute(target);auto expectedCrc=crc(gpu.readback(expected.output()));
        auto count=executor.statistics.scenes;playback.seek(31);auto seekExecutions=executor.statistics.scenes-count;
        bool seekRecord=playback.ordinal()==4&&playback.timestamp()==50;
        bool nativeCamera=crc(gpu.readback(executor.output()))==expectedCrc;
        count=executor.statistics.scenes;playback.seekOrdinal(1);bool backwardOne=executor.statistics.scenes-count==1;
        count=executor.statistics.scenes;playback.seekOrdinal(4);bool forwardOne=executor.statistics.scenes-count==1;
        playback.seekOrdinal(0);auto before=crc(gpu.readback(executor.output()));auto oldOrdinal=playback.ordinal();
        count=executor.statistics.scenes;bool cancel=false;
        bool accepted=playback.seekOrdinal(5,[&]{return cancel;},[&](uint64_t ordinal){if(ordinal>=3)cancel=true;});
        bool cancelNoExecution=!accepted&&executor.statistics.scenes==count;
        bool cancelPreserves=playback.ordinal()==oldOrdinal&&crc(gpu.readback(executor.output()))==before;
        count=executor.statistics.scenes;playback.select(11);bool cursorPreserved=playback.ordinal()==3&&executor.statistics.scenes-count==1;
        playback.seek(10);bool firstDuplicate=playback.ordinal()==1;count=executor.statistics.scenes;
        playback.step(1);bool nextDuplicate=playback.ordinal()==2&&playback.timestamp()==10&&executor.statistics.scenes-count==1;
        count=executor.statistics.scenes;playback.seek(50,true);bool endpoint=playback.ordinal()==5&&playback.ended()&&executor.statistics.scenes-count==1;
        std::cout<<"{\"seek_executions\":"<<seekExecutions<<",\"record\":"<<(seekRecord?"true":"false")
            <<",\"native_camera\":"<<(nativeCamera?"true":"false")<<",\"backward_one\":"<<(backwardOne?"true":"false")
            <<",\"forward_one\":"<<(forwardOne?"true":"false")<<",\"cancel_no_execution\":"<<(cancelNoExecution?"true":"false")
            <<",\"cancel_preserves\":"<<(cancelPreserves?"true":"false")<<",\"cursor_preserved\":"<<(cursorPreserved?"true":"false")
            <<",\"first_duplicate\":"<<(firstDuplicate?"true":"false")<<",\"next_duplicate\":"<<(nextDuplicate?"true":"false")
            <<",\"endpoint\":"<<(endpoint?"true":"false")<<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
