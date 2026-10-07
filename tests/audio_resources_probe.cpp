#include "audio_resources.hpp"
#include <cmath>
#include <future>
#include <iostream>
#include <sstream>

static std::string json(std::string_view text){
    std::string out="\"";for(unsigned char c:text){if(c=='\\'||c=='\"'){out+='\\';out+=c;}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else if(c<32)out+='?';else out+=c;}return out+'\"';
}
int wmain(int argc,wchar_t** argv){try{
    if(argc<4)return 2;rep::AudioResources resources(argv[2],argv[3]);auto mode=rep::utf8(argv[1]);
    if(mode=="cancel-retry"){
        auto cancelled=resources.clip("TONE",[]{return true;});auto retry=resources.clip("TONE");
        std::cout<<"{\"cancelled_null\":"<<(cancelled?"false":"true")<<",\"retry_frames\":"<<(retry?retry->frames():0)<<",\"decoded\":"<<resources.decoded()<<"}";return 0;
    }
    if(mode=="concurrent"){
        auto first=std::async(std::launch::async,[&]{return resources.clip("TONE");});auto second=std::async(std::launch::async,[&]{return resources.clip("ALIAS");});auto a=first.get(),b=second.get();
        std::cout<<"{\"same\":"<<(a&&a==b?"true":"false")<<",\"frames\":"<<(a?a->frames():0)<<",\"decoded\":"<<resources.decoded()<<"}";return 0;
    }
    std::cout<<"{\"items\":[";bool comma=false;for(int i=4;i<argc;i++){
        auto tag=rep::utf8(argv[i]);auto definition=resources.definition(tag);auto clip=resources.clip(tag);
        if(comma)std::cout<<',';comma=true;std::cout<<"{\"tag\":"<<json(tag)<<",\"definition\":"<<(definition?"true":"false");
        if(definition)std::cout<<",\"file\":"<<json(definition->file)<<",\"playable\":"<<(definition->playable?"true":"false")<<",\"kind\":"<<int(definition->kind)<<",\"loop_delay\":"<<definition->loopDelay<<",\"gain\":"<<definition->gain<<",\"duplicate_limit\":"<<definition->duplicateLimit<<",\"duplicate_policy\":"<<json(definition->duplicatePolicy);
        std::cout<<",\"clip\":"<<(clip?"true":"false");if(clip){double energy=0,peak=0;bool finite=true;for(float sample:clip->samples){energy+=std::abs(sample);peak=std::max(peak,double(std::abs(sample)));finite=finite&&std::isfinite(sample);}std::cout<<",\"frames\":"<<clip->frames()<<",\"rate\":"<<clip->sampleRate<<",\"channels\":"<<clip->channels<<",\"mean_abs\":"<<energy/clip->samples.size()<<",\"peak\":"<<peak<<",\"finite\":"<<(finite?"true":"false");}std::cout<<'}';
    }
    std::cout<<"],\"decoded\":"<<resources.decoded()<<",\"missing\":"<<resources.missing()<<",\"diagnostics\":[";comma=false;for(auto& value:resources.diagnostics()){if(comma)std::cout<<',';comma=true;std::cout<<json(value);}std::cout<<"]}";return 0;
}catch(const std::exception& error){std::cerr<<error.what();return 1;}}
