#pragma once
#include "protocol.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace rep {
enum class CanvasMode { Multiple, Size, Padding };
struct CanvasLayout {
    int width=800,height=600,left=0,top=0,right=0,bottom=0;
    bool operator==(const CanvasLayout&)const=default;
};
struct CanvasSettings {
    CanvasMode mode=CanvasMode::Multiple;
    double factor=1;
    int width=1920,height=1080,left=256,top=256,right=256,bottom=256;
    bool operator==(const CanvasSettings&)const=default;
    CanvasLayout resolve(int originalWidth,int originalHeight)const {
        constexpr int maximum=16384;
        if(originalWidth<=0||originalHeight<=0||originalWidth>maximum||originalHeight>maximum)
            throw Error("Invalid original canvas size");
        CanvasLayout result;
        if(mode==CanvasMode::Padding){
            if(left<0||top<0||right<0||bottom<0)throw Error("Canvas padding must be nonnegative");
            int64_t w=int64_t(originalWidth)+left+right,h=int64_t(originalHeight)+top+bottom;
            if(w>maximum||h>maximum)throw Error("Canvas dimensions exceed 16384 pixels");
            result={int(w),int(h),left,top,right,bottom};
        }else{
            double w,h;
            if(mode==CanvasMode::Multiple){
                if(!std::isfinite(factor)||factor<1)throw Error("Canvas scale must be finite and at least 1");
                w=std::ceil(originalWidth*factor);h=std::ceil(originalHeight*factor);
            }else if(mode==CanvasMode::Size){
                if(width<=0||height<=0)throw Error("Canvas dimensions must be positive");
                // A saved fixed-size preference must also accommodate a larger REP.
                w=std::max(width,originalWidth);h=std::max(height,originalHeight);
            }else throw Error("Unknown canvas mode");
            if(w>maximum||h>maximum)throw Error("Canvas dimensions exceed 16384 pixels");
            result.width=int(w);result.height=int(h);
            result.left=(result.width-originalWidth)/2;result.top=(result.height-originalHeight)/2;
            result.right=result.width-originalWidth-result.left;result.bottom=result.height-originalHeight-result.top;
        }
        return result;
    }
};
inline bool parseCanvasOption(CanvasSettings& settings,std::wstring_view key,std::wstring_view value){
    if(key!=L"--canvas-scale"&&key!=L"--canvas-size"&&key!=L"--canvas-padding")return false;
    auto integer=[](std::wstring_view text){
        size_t used=0;long long number=std::stoll(std::wstring(text),&used);
        if(used!=text.size()||number<0||number>std::numeric_limits<int>::max())throw Error("Invalid canvas integer");
        return int(number);
    };
    auto candidate=settings;
    try{
        if(key==L"--canvas-scale"){
            size_t used=0;candidate.factor=std::stod(std::wstring(value),&used);
            if(used!=value.size())throw Error("Invalid canvas scale");
            candidate.mode=CanvasMode::Multiple;
        }else if(key==L"--canvas-size"){
            size_t separator=value.find_first_of(L"xX");
            if(separator==std::wstring_view::npos)throw Error("Canvas size must be WIDTHxHEIGHT");
            candidate.width=integer(value.substr(0,separator));candidate.height=integer(value.substr(separator+1));candidate.mode=CanvasMode::Size;
        }else{
            std::array<int,4> values{};size_t begin=0;
            for(size_t n=0;n<4;n++){
                size_t end=value.find(L',',begin);
                if((n<3&&end==std::wstring_view::npos)||(n==3&&end!=std::wstring_view::npos))throw Error("Canvas padding must be LEFT,TOP,RIGHT,BOTTOM");
                values[n]=integer(value.substr(begin,end==std::wstring_view::npos?end:end-begin));begin=end+1;
            }
            candidate.left=values[0];candidate.top=values[1];candidate.right=values[2];candidate.bottom=values[3];candidate.mode=CanvasMode::Padding;
        }
        candidate.resolve(1,1);
    }catch(const std::invalid_argument&){throw Error("Invalid canvas option value");}
    catch(const std::out_of_range&){throw Error("Canvas option value is out of range");}
    settings=candidate;return true;
}
}
