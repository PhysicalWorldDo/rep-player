#include "engine.hpp"
#include "movies.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <bit>

namespace rep {
static std::string imagePath(std::string path){path=canonical(std::move(path));while(!path.empty()&&path.front()=='/')path.erase(path.begin());if(!path.ends_with(".img"))return {};if(!path.starts_with("sprite/"))path="sprite/"+path;return path;}
static void mergeImage(std::vector<ImgCall>& images,ImgCall value,bool addCounts=true){
    auto it=std::find_if(images.begin(),images.end(),[&](const ImgCall& i){return i.path==value.path;});
    if(it==images.end()){value.frame=-1;images.push_back(std::move(value));return;}
    if(value.count){if(!it->count){it->role=value.role;it->dependency=value.dependency;}else{if(it->role!=value.role)it->role="mixed";it->dependency=it->dependency&&value.dependency;}}
    it->timelineCount=addCounts?it->timelineCount+value.timelineCount:std::max(it->timelineCount,value.timelineCount);
    it->runtimeCount+=value.runtimeCount;it->count=std::max(it->timelineCount,it->runtimeCount);it->drawn|=value.drawn;it->registered|=value.registered;
}
static void registeredImages(Replay& replay,std::vector<ImgCall>& images){for(const auto& resource:replay.resources){auto path=imagePath(resource);if(!path.empty()){ImgCall value;value.path=std::move(path);value.role="registered";value.registered=true;mergeImage(images,std::move(value));}}}
static std::vector<ImgCall> dispatchedImages(const Replay& replay,const Instruction& i,std::span<const uint8_t> payload,uint32_t layer){
    std::vector<ImgCall> calls;auto add=[&](std::string path,int frame,std::string role="draw",bool dependency=false){path=imagePath(std::move(path));if(!path.empty()){ImgCall call;call.path=std::move(path);call.frame=frame;call.count=call.timelineCount=1;call.layer=layer;call.role=std::move(role);call.dependency=dependency;calls.push_back(std::move(call));}};
    if(i.opcode==3||i.opcode==4||(i.opcode>=42&&i.opcode<=47)||i.opcode==57)add(replay.path(i.resource),i.frame);
    else if(i.opcode==10)for(auto instance:i.instances)add(replay.path(i.resource),instance[1]);
    else if((i.opcode==18||i.opcode==64)&&!payload[4])add("sprite/character/defaultfaces.img",payload[5]);
    else if(i.opcode==58||(i.opcode==15&&payload[0]))add(replay.path(i.resource),i.frame,"mask",true);
    return calls;
}
ReplayInspection inspectReplayImages(Replay& replay,const std::function<bool()>& cancelled){
    ReplayInspection result;registeredImages(replay,result.images);replay.rewind();Scene scene;
    while(replay.next(scene)){if(cancelled&&cancelled())return result;result.durationMilliseconds=std::max(result.durationMilliseconds,scene.timestamp);result.scenes++;uint32_t layerId=0;Reader aux(scene.aux);std::vector<const Effect*> effects;unsigned phantom=0;int stencilPhase=0,stencilChannel=-1;
        for(auto id:scene.ids)if(auto command=replay.command(id))for(const auto& i:command->instructions){auto payload=std::span(command->raw).subspan(i.offset,i.payloadBytes);aux.take(i.auxBytes);if(i.opcode==16)layerId=payload[0];else if(i.opcode==17)layerId=0;
            if(i.opcode==19&&i.effect.present&&!i.effect.obsolete){if(i.effect.programNull())phantom++;else effects.push_back(&i.effect);}else if(i.opcode==20){if(phantom)phantom--;else if(!effects.empty())effects.pop_back();}else if(i.opcode==40&&i.nativeContextState&&!effects.empty())effects.pop_back();
            if((i.opcode==26||i.opcode==27)&&payload[0]<3){stencilChannel=payload[0];stencilPhase=i.opcode==26?1:2;}else if(i.opcode==29){stencilChannel=-1;stencilPhase=0;}
            auto calls=dispatchedImages(replay,i,payload,i.opcode>=42?i.layer:layerId);bool drawing=std::any_of(calls.begin(),calls.end(),[](const ImgCall& call){return !call.dependency;})||i.opcode==5||i.opcode==48;
            for(auto& call:calls)if(!call.dependency){if(stencilPhase==1&&(stencilChannel==0||stencilChannel==2)){call.role="stencil";call.dependency=true;}else if(i.opcode>=45&&i.opcode<=47)call.role="masked-draw";}
            for(auto& call:calls)mergeImage(result.images,std::move(call));
            if(drawing)for(auto& input:shaderImageInputs(effects.empty()?nullptr:effects.back(),replay)){ImgCall call;call.path=imagePath(input.path);call.frame=input.frame;call.count=call.timelineCount=1;call.layer=layerId;call.role="shader-input";call.dependency=true;if(!call.path.empty())mergeImage(result.images,std::move(call));}}
        aux.end();}
    std::sort(result.images.begin(),result.images.end(),[](const ImgCall& a,const ImgCall& b){return a.path<b.path;});return result;
}
struct Matrix {
    double a=1,b=0,c=0,d=1,x=0,y=0;
    std::array<float,2> apply(double xx,double yy)const{return {float(a*xx+b*yy+x),float(c*xx+d*yy+y)};}
    void multiply(double aa,double bb,double cc,double dd){double na=a*aa+b*cc,nb=a*bb+b*dd,nc=c*aa+d*cc,nd=c*bb+d*dd;a=na;b=nb;c=nc;d=nd;}
};
struct Executor::State {
    struct Blend{unsigned type,value;};
    std::vector<Blend> blends;
    struct EffectBinding {
        const Effect* effect=nullptr;
        std::shared_ptr<GpuTexture> mask;
        const Effect* operator->()const{return effect;}
    };
    std::vector<EffectBinding> effects;
    unsigned phantom=0,flags=0,colorMode=0,color=0;
    std::vector<unsigned> flagsStack;
    std::array<int,4> clip{};
    bool worldClip=false;
    bool nextClip=false,samplerEnabled=false;
    struct Sampler{unsigned number=0,flag=0;float sx=1,sy=1,x=0,y=0,ox=0,oy=0;};
    std::optional<Sampler> sampler;
    std::vector<std::optional<Sampler>> samplers;
    int channel=-1,phase=0,reference=0;
    bool invert=false;
    std::array<int,3> nextStencil{1,64,128};
    std::array<std::set<int>,3> hiddenStencil;
    struct Grid {std::vector<int> xs,ys;};
    std::map<std::pair<int64_t,int>,Grid> grids;
    uint32_t layer=0;int context=10,savedContext=0,nextContext=218;
};
struct Executor::Sprite {
    const Instruction* instruction=nullptr;
    uint32_t opcode=3,layer=0;int context=10,frameIndex=0;
    bool explicitCamera=false,bypassZoom=false,offscreen=false,skipGrid=false,directGridExtent=false,font=false,gradient=false,update=false,specialUv=false;
    int channel=0;uint32_t lower=0;
    int64_t resource=-1;
    std::shared_ptr<Frame> frame;
    std::array<uint8_t,64> params=drawDefaults();
    float x=0,y=0;
    std::optional<Matrix> matrix;
    std::optional<std::array<float,4>> sourceRect;
    std::optional<State::Grid> grid;
    std::optional<std::array<int,2>> sourceSize;
    std::optional<std::array<int,4>> screenClip;
    size_t call=SIZE_MAX;
    size_t actualCall=SIZE_MAX;
};
static int cameraId(int context,std::optional<uint32_t> target={}){
    if(context==10||context==81)return 81;
    if((context==82||context==83||context==84)&&target)return *target==10?83:*target==11?84:82;
    return context;
}
static void layer(Executor::State& state,const Instruction& i,std::span<const uint8_t> payload,const Replay& r){
    state.layer=i.opcode==16?payload[0]:0;
    if(i.opcode==16&&r.header.renderMode==2)state.context=81+state.layer;
    else if(i.opcode==16&&r.header.renderMode==1&&r.version<17)state.context=70+4*((state.layer-1)&255);
    else if(r.header.renderMode!=2&&r.version<17)state.context=10;
}
static void contextState(Executor::State& state,const Instruction& i){
    if(!i.nativeContextState)return;
    if(i.opcode==38){state.savedContext=state.context;state.nextContext=std::min(state.nextContext+1,230);state.context=state.nextContext;}
    else if(i.opcode==39)state.context=state.savedContext;
}
Executor::Executor(Gpu& gpu,Assets& assets,const std::filesystem::path& cache,const std::filesystem::path& clientRoot):gpu_(gpu),assets_(assets),binder_(gpu,assets),fonts_((clientRoot.empty()?assets.root().parent_path():clientRoot)/L"Fonts"),movies_(std::make_unique<Movies>(clientRoot.empty()?assets.root().parent_path():clientRoot,cache/L"movies")){}
Executor::~Executor()=default;
bool Executor::hidden(const std::string& path)const{return hiddenImages_.contains(imagePath(path));}
void Executor::setHiddenImages(std::unordered_set<std::string> paths){hiddenImages_.clear();for(auto& path:paths){auto normalized=imagePath(path);if(!normalized.empty())hiddenImages_.insert(std::move(normalized));}for(auto& call:currentImages_)call.hidden=hidden(call.path);for(auto& call:allImages_)call.hidden=hidden(call.path);}
size_t Executor::recordImage(std::string path,int frame,uint32_t layerId,std::string role,bool dependency,bool drawn){
    path=imagePath(std::move(path));if(path.empty())return SIZE_MAX;ImgCall value;value.path=std::move(path);value.frame=frame;value.layer=layerId;value.role=std::move(role);value.dependency=dependency;value.drawn=drawn;value.count=value.runtimeCount=1;value.hidden=hidden(value.path);
    auto it=std::find_if(currentImages_.begin(),currentImages_.end(),[&](const ImgCall& call){return call.path==value.path&&call.frame==value.frame&&call.layer==value.layer&&call.role==value.role;});size_t index;
    if(it==currentImages_.end()){index=currentImages_.size();currentImages_.push_back(value);}else{it->count++;it->drawn|=drawn;index=it-currentImages_.begin();}
    mergeImage(allImages_,value);return index;
}
void Executor::markDrawn(size_t call){if(call==SIZE_MAX)return;currentImages_[call].drawn=true;auto& path=currentImages_[call].path;for(auto& image:allImages_)if(image.path==path){image.drawn=true;break;}}
void Executor::setInspection(ReplayInspection inspection){durationMilliseconds_=inspection.durationMilliseconds;for(auto& image:inspection.images){image.hidden=hidden(image.path);mergeImage(allImages_,std::move(image),false);}}
void Executor::resetPlaybackState(){gpu_.flush();for(auto& pixels:movies_->pixels())gpu_.forgetTexture(pixels.get());cameras_.clear();samplerOffsets_.clear();lastBlend_=0;movies_->reset();localCapture_=globalCapture_=localHidden_=globalHidden_=false;binder_.setReplay(replay_);selectedContext_=219;contextBound_.fill(false);sceneEntryValid_=false;}
Target& Executor::renderTarget(int context){if(context>=219&&context<=230&&contexts_[context-219])return *contexts_[context-219];return output_;}
void Executor::attach(Replay& replay){
    gpu_.flush();
    replay_=&replay;statistics={};resetPlaybackState();currentImages_.clear();allImages_.clear();durationMilliseconds_=0;registeredImages(replay,allImages_);
    int w=replay.header.width(),h=replay.header.height();if(output_.image.width!=w||output_.image.height!=h){output_=gpu_.target(w,h);canvas_=gpu_.target(w,h,false);working_=gpu_.target(w,h,false);raw_=gpu_.target(w,h,false);localMask_=gpu_.target(w,h,false);globalMask_=gpu_.target(w,h,false);for(auto& a:actors_)a.reset();for(auto& c:contexts_)c.reset();}
    float black[4]={0,0,0,transparent_?0.f:1.f};gpu_.begin(output_,black);
}
void Executor::prepare(const std::function<bool()>& cancelled){
    if(!replay_)return;std::set<std::pair<std::string,int>> requested;
    for(const auto& [id,command]:replay_->dictionary)for(const auto& i:command.instructions){
        auto path=replay_->path(i.resource);
        if(i.opcode==3||i.opcode==4||i.opcode==10||(i.opcode>=42&&i.opcode<=47)||i.opcode==57||i.opcode==58||i.opcode==15){
            if(i.opcode==10)for(auto value:i.instances)requested.emplace(path,value[1]);else requested.emplace(path,i.frame);}
        if(i.opcode==18||i.opcode==64){auto payload=std::span(command.raw).subspan(i.offset,i.payloadBytes);if(!payload[4])requested.emplace("sprite/character/defaultfaces.img",payload[5]);}
        if(i.opcode==0){auto payload=std::span(command.raw).subspan(i.offset,i.payloadBytes);if((at<uint32_t>(payload,0)&255)==7)assets_.preload(replay_->path(at<uint32_t>(payload,8)));}
    }
    for(const auto& [path,index]:requested){if(cancelled&&cancelled())return;if(path.empty())continue;auto f=assets_.frame(path,index);if(f&&!f->empty)gpu_.texture(f->texture);}
}
static Matrix affine(const Executor::Sprite& s,const Frame& frame){
    const auto& p=s.params;double px=at<float>(p,28),py=at<float>(p,32);if(px>=4.29e9&&py>=4.29e9){px=frame.fullWidth/2;py=frame.fullHeight/2;}
    // Both client matrix backends (1487E0640 / 1487D42E0) negate the
    // recorded angle before generating rotation, including the scale axes.
    double sx=at<float>(p,20),sy=at<float>(p,24),angle=-at<float>(p,16),c=std::cos(angle),sn=std::sin(angle),direction=p[6]==1?-1:1;
    Matrix m;if(s.opcode==4){m.a=direction*c*sx;m.b=-direction*sn*sy;m.c=sn*sx;m.d=c*sy;}
    else {m.a=direction*sx*c;m.b=-direction*sx*sn;m.c=sy*sn;m.d=sy*c;}
    double extra=-at<float>(p,48),ex=at<float>(p,52),ey=at<float>(p,56);c=std::cos(extra);sn=std::sin(extra);m.multiply(ex*c*c+ey*sn*sn,(ex-ey)*c*sn,(ex-ey)*c*sn,ex*sn*sn+ey*c*c);
    m.x=s.x+px+m.a*(frame.x-px)+m.b*(frame.y-py);m.y=s.y+py+m.c*(frame.x-px)+m.d*(frame.y-py);return m;
}
static std::array<float,4> color(uint32_t argb){return {float((argb>>16)&255)/255,float((argb>>8)&255)/255,float(argb&255)/255,float(argb>>24)/255};}
static double mapAxis(double position,double full,double target,const std::vector<int>& intervals){
    size_t n=intervals.size()/2*2;if(!n)return full?position*target/full:0;double stretch=0;for(size_t k=0;k<n;k+=2)stretch+=std::max(0,intervals[k+1]-intervals[k]);double scale=stretch>0?std::max((target-(full-stretch))/stretch,0.):1,destination=0,previous=0;
    for(size_t k=0;k<n;k+=2){int begin=intervals[k],end=intervals[k+1];if(position<=begin)return destination+position-previous;destination+=begin-previous;if(position<=end)return destination+(position-begin)*scale;destination+=(end-begin)*scale;previous=end;}return destination+position-previous;
}
static std::array<int,4> cameraClip(std::array<int,4> clip,const Camera& camera,bool bypassZoom){
    // GameRenderCamera virtual128 (CN 148C2D900) transforms the drawable's
    // world rectangle separately from its floating-point sprite matrix.
    constexpr std::array<int,4> unbounded{-1000000,-1000000,1000000,1000000};
    int origin[]={int(camera.x),int(camera.y)},size[]={int(camera.width),int(camera.height)};
    bool zoom=(origin[0]||origin[1])&&!bypassZoom&&std::abs(camera.zoom-1.f)>std::abs(camera.zoom)*0x1p-23f;
    for(int n=0;n<4;n++){
        int axis=n%2;
        if(clip[n]==unbounded[n])clip[n]=n<2?0:size[axis];
        else {clip[n]-=origin[axis];if(zoom){int center=size[axis]/2;clip[n]=center+int(float(clip[n]-center)*camera.zoom);}}
        clip[n]=n<2?std::min(clip[n],size[axis]):std::max(clip[n],0);
    }
    return clip;
}
void Executor::draw(Sprite s,State& state){
    statistics.primitives++;
    if(!s.frame){auto path=s.opcode==18||s.opcode==64?std::string("sprite/character/defaultfaces.img"):replay_->path(s.resource);if(path.empty()){statistics.nullResources++;return;}s.frame=assets_.frame(path,s.frameIndex);}
    if(s.call==SIZE_MAX&&!s.font&&s.frame){auto path=imagePath(s.instruction&&(s.instruction->opcode==18||s.instruction->opcode==64)?std::string("sprite/character/defaultfaces.img"):replay_->path(s.resource));bool stencil=state.phase==1&&(state.channel==0||state.channel==2);bool fallback=!s.frame->actualPath.empty()&&(s.frame->actualPath!=path||s.frame->actualFrame!=s.frameIndex);
        s.call=recordImage(path,s.frameIndex,s.layer,fallback?(stencil?"stencil-request":"missing-request"):stencil?"stencil":s.offscreen?"masked-draw":"draw",stencil);
        if(fallback)s.actualCall=recordImage(s.frame->actualPath,s.frame->actualFrame,s.layer,stencil?"stencil-placeholder":"placeholder",stencil);}
    if(!s.frame)return;if(s.frame->empty){statistics.emptyImages++;return;}
    bool captureActive=s.offscreen?localCapture_:globalCapture_;if(s.offscreen&&!captureActive){statistics.nullCaptures++;return;}
    auto frame=*s.frame;
    if(!s.skipGrid){auto it=state.grids.find({s.resource,s.frameIndex});auto grid=s.grid?s.grid:(it!=state.grids.end()?std::optional{it->second}:std::nullopt);
        if(grid){
            double targetX=s.directGridExtent?at<float>(s.params,20):frame.fullWidth-frame.width+frame.width*at<float>(s.params,20),targetY=s.directGridExtent?at<float>(s.params,24):frame.fullHeight-frame.height+frame.height*at<float>(s.params,24),sx=at<float>(s.params,52),sy=at<float>(s.params,56);
            std::vector<double> xs{double(frame.x),double(frame.x+frame.width)},ys{double(frame.y),double(frame.y+frame.height)};
            for(size_t i=0;i<grid->xs.size()/2*2;i++)xs.push_back(std::clamp<double>(grid->xs[i],frame.x,frame.x+frame.width));for(size_t i=0;i<grid->ys.size()/2*2;i++)ys.push_back(std::clamp<double>(grid->ys[i],frame.y,frame.y+frame.height));
            auto unique=[](auto& v){std::sort(v.begin(),v.end());v.erase(std::unique(v.begin(),v.end()),v.end());};unique(xs);unique(ys);
            for(size_t y=1;y<ys.size();y++)for(size_t x=1;x<xs.size();x++){
                double left=mapAxis(xs[x-1],frame.fullWidth,targetX,grid->xs),right=mapAxis(xs[x],frame.fullWidth,targetX,grid->xs),top=mapAxis(ys[y-1],frame.fullHeight,targetY,grid->ys),bottom=mapAxis(ys[y],frame.fullHeight,targetY,grid->ys);
                if(right<=left||bottom<=top)continue;auto cell=s;cell.opcode=3;cell.skipGrid=true;cell.grid.reset();put(cell.params,20,float(sx*(right-left)/(xs[x]-xs[x-1])));put(cell.params,24,float(sy*(bottom-top)/(ys[y]-ys[y-1])));put(cell.params,28,0.f);put(cell.params,32,0.f);put(cell.params,52,1.f);put(cell.params,56,1.f);
                cell.frame=std::make_shared<Frame>(frame);cell.frame->width=xs[x]-xs[x-1];cell.frame->height=ys[y]-ys[y-1];cell.frame->x=0;cell.frame->y=0;cell.sourceRect=std::array<float,4>{float(xs[x-1]-frame.x),float(ys[y-1]-frame.y),float(xs[x]-frame.x),float(ys[y]-frame.y)};
                auto m=affine(cell,*cell.frame);m.x+=sx*left;m.y+=sy*top;cell.matrix=m;draw(std::move(cell),state);statistics.gridCells++;}
            return;
        }
    }
    int originalWidth=s.sourceSize?s.sourceSize->at(0):frame.width,originalHeight=s.sourceSize?s.sourceSize->at(1):frame.height;std::array<float,4> source{s.sourceRect.value_or(std::array<float,4>{0,0,float(frame.width),float(frame.height)})};
    bool quad2=s.instruction&&s.instruction->opcode>=42&&s.instruction->opcode<=47&&(s.instruction->opcode-42)%3==2;
    if(quad2){const auto& n=s.instruction->native;source={0,0,float(frame.width),float(frame.height)};
        if(n[16])for(int i=0;i<4;i++)source[i]=float(int(at<float>(n,20+i*4)));else if(n[72]){source[2]+=frame.x;source[3]+=frame.y;}
        if(n[36]){put(s.params,20,(at<float>(n,48)-at<float>(n,40))/frame.fullWidth);put(s.params,24,(at<float>(n,52)-at<float>(n,44))/frame.fullHeight);}
        frame.x=frame.y=0;put(s.params,28,0.f);put(s.params,32,0.f);s.params[6]=0;s.opcode=4;
    }
    if(s.params[36]){for(int i=0;i<4;i++)source[i]=at<int16_t>(s.params,38+i*2);if(source[2]<=source[0]||source[3]<=source[1])return;put(s.params,20,at<float>(s.params,20)*(source[2]-source[0])/frame.width);put(s.params,24,at<float>(s.params,24)*(source[3]-source[1])/frame.height);}
    if(s.specialUv?(source[2]==source[0]||source[3]==source[1]):(source[2]<=source[0]||source[3]<=source[1]))return;
    Matrix m=s.matrix.value_or(affine(s,frame));
    if(!s.matrix||s.skipGrid){int context=s.context>=81&&s.context<=84?int(s.layer):s.context;bool eligible=state.samplerEnabled&&state.sampler&&state.sampler->number==1&&(context==10||context==11||context==28||context==29||context==30||context==33);
        if(eligible){auto& sampler=*state.sampler;if(sampler.flag==1)samplerOffsets_[context]={sampler.x,sampler.y};auto offset=samplerOffsets_[context];m.a*=sampler.sx;m.b*=sampler.sx;m.c*=sampler.sy;m.d*=sampler.sy;m.x=sampler.sx*(m.x-sampler.ox)+offset[0];m.y=sampler.sy*(m.y-sampler.oy)+offset[1];statistics.samplerDraws++;}else samplerOffsets_.erase(context);
    }
    auto clip=s.screenClip.value_or(state.clip);
    double cameraZoom=1,cameraScaleX=1,cameraScaleY=1;
    if(s.explicitCamera){
        auto it=cameras_.find({cameraId(s.context),s.layer});
        if(it!=cameras_.end()){
            const auto& c=it->second;if(c.width==0||c.height==0)return;
            if(state.worldClip&&!s.screenClip){
                constexpr std::array<int,4> unbounded{-1000000,-1000000,1000000,1000000};
                clip=state.clip==unbounded?std::array{0,0,output_.image.width,output_.image.height}:cameraClip(state.clip,c,s.bypassZoom);
            }
            double zoom=s.bypassZoom?1:c.zoom;
            cameraZoom=zoom;cameraScaleX=zoom*output_.image.width/c.width;cameraScaleY=zoom*output_.image.height/c.height;
            m.x=output_.image.width*.5+cameraScaleX*(m.x-c.x-c.width*.5);
            m.y=output_.image.height*.5+cameraScaleY*(m.y-c.y-c.height*.5);
        }else{
            // Native GameRenderCamera creates absent layer entries with identity
            // view/projection (CN 148CA8E20 -> 148CA8800). Coordinates are clip
            // space until opcode50 initializes that layer, not screen pixels.
            cameraScaleX=output_.image.width*.5;cameraScaleY=-output_.image.height*.5;
            m.x=(m.x+1)*cameraScaleX;m.y=(m.y-1)*cameraScaleY;
        }
        m.a*=cameraScaleX;m.b*=cameraScaleX;m.c*=cameraScaleY;m.d*=cameraScaleY;
    }
    if(std::abs(m.a*m.d-m.b*m.c)<1e-12)return;
    if(s.params[12]>=1&&s.params[12]<=3&&!s.matrix){double px=at<float>(s.params,28),py=at<float>(s.params,32);if(px>=4.29e9&&py>=4.29e9){px=frame.fullWidth/2;py=frame.fullHeight/2;}
        if(frame.rotated&&!s.params[36]&&!s.sourceRect)source={0,0,float(frame.height),float(frame.width)};
        int ax=int(px)-frame.x,ay=int(py)-frame.y,mode=s.params[12],sourceWidth=int(source[2]-source[0]),sourceHeight=int(source[3]-source[1]);if(frame.rotated)std::swap(sourceWidth,sourceHeight);
        int w=mode==1||mode==3?std::min(ax,sourceWidth):sourceWidth,h=mode==2||mode==3?std::min(ay,sourceHeight):sourceHeight;if(w==0||h==0)return;
        for(int my=0;my<((mode==2||mode==3)?2:1);my++)for(int mx=0;mx<((mode==1||mode==3)?2:1);mx++){auto part=s;part.params[12]=part.params[36]=0;part.specialUv=true;part.sourceSize=std::array{originalWidth,originalHeight};part.screenClip=clip;part.frame=std::make_shared<Frame>(frame);part.frame->width=w;part.frame->height=h;part.sourceRect=std::array<float,4>{source[0],source[1],source[0]+(frame.rotated?h:w),source[1]+(frame.rotated?w:h)};if(frame.rotated&&(mode==2||mode==3)){part.sourceRect->at(0)=source[2]-h;part.sourceRect->at(2)=source[2];}auto reflected=m;reflected.x+=mx?2*ax*m.a:0;reflected.y+=mx?2*ax*m.c:0;reflected.x+=my?2*ay*m.b:0;reflected.y+=my?2*ay*m.d:0;reflected.a*=mx?-1:1;reflected.c*=mx?-1:1;reflected.b*=my?-1:1;reflected.d*=my?-1:1;part.matrix=reflected;part.explicitCamera=false;draw(std::move(part),state);}return;
    }
    auto sourceTexture=gpu_.texture(frame.texture,s.update);ShaderContext c;c.version=replay_->version;c.minor=replay_->header.minor;c.width=originalWidth;c.height=originalHeight;c.channel=s.channel;c.imagePresent=true;c.directSource=s.font||s.sourceRect.has_value()||s.params[36]||(quad2&&s.instruction->native[16]);c.x=m.x;c.y=m.y;c.scaleX=at<float>(s.params,20);c.scaleY=at<float>(s.params,24);c.direction=s.params[6]==1?0:1;
    auto& destination=renderTarget(s.context);
    const Effect* effect=state.effects.empty()?nullptr:state.effects.back().effect;
    Effect contextEffect;
    if(effect&&state.effects.back().mask){
        auto mask=state.effects.back().mask;contextEffect=*effect;
        if(contextEffect.floats.size()!=7)contextEffect.floats.assign(7,0);
        // 1472FC8C0 normalizes sprite origin and integer scaled source size
        // against the selected render texture before binding native type54.
        contextEffect.floats[0]=float(m.x)/mask->width;contextEffect.floats[1]=float(m.y)/mask->height;
        contextEffect.floats[2]=int(originalWidth*c.scaleX)*float(cameraScaleX)/mask->width*(c.direction==1?1:-1);
        contextEffect.floats[3]=int(originalHeight*c.scaleY)*float(cameraScaleY)/mask->height;
        c.external[1]=std::move(mask);effect=&contextEffect;
    }
    if(effect&&(effect->type==4||effect->type==5||effect->type==29||(effect->type==33&&effect->bits.size()>=6&&std::bit_cast<float>(effect->bits[5])==1))){gpu_.capture(destination,canvas_);c.canvas=gpu_.view(canvas_);}
    auto material=binder_.bind(effect,sourceTexture,c);bool dependencyHidden=false;for(auto& input:material.inputs){recordImage(input.path,input.frame,s.layer,"shader-input",true);dependencyHidden|=hidden(input.path);}unsigned flags=state.flags;unsigned tint=0;bool tinted=false;unsigned colorMode=state.colorMode,colorWord=state.color;
    for(auto blend:state.blends){switch(blend.type){case 0:if(!state.flags)flags=0;break;case 1:if(!state.flags)flags=1;break;case 2:if(!state.flags)flags=2;break;case 3:if(!state.flags)flags=4;break;case 4:if(!state.flags)flags=8;break;case 11:if(!state.flags)flags=16;break;case 12:if(!state.flags)flags=128;break;case 13:if(!state.flags)flags=64;break;case 5:tint=blend.value;tinted=true;break;case 8:colorMode=2;colorWord=blend.value;break;default:throw Error("invalid drawn blend type");}}
    if(s.font){colorMode=0;tinted=false;}if(colorMode==1){tint=colorWord;tinted=true;}if(flags==16)flags=lastBlend_;else lastBlend_=flags;if(material.blend)flags=material.blend;material.blend=flags;
    uint32_t argb=at<uint32_t>(s.params,8);auto vertexColor=color(argb);if(tinted)for(int i=0;i<3;i++)vertexColor[i]=float((tint>>(i*8))&255)/255;
    if(!effect&&colorMode==2){material.program=1;material.parameters[0]=float(colorWord)/255;}else if(!effect&&tinted){material.program=2;material.elapsed=4;}
    if(colorMode>2)throw Error("invalid pipeline color mode");
    bool directUv=c.directSource||material.program==2;
    auto scaled=[](float scale){return std::abs(std::abs(scale)-1.f)>std::abs(scale)*0x1p-23f;};
    bool rotated=at<float>(s.params,16)!=0;
    bool inset=!s.font&&(frame.atlas||directUv||(frame.texture->logicalWidth&&frame.texture->logicalWidth<=512&&frame.texture->logicalHeight<=512));
    float insetX=inset&&(scaled(at<float>(s.params,20))||rotated||cameraZoom!=1)?.5f:0;
    float insetY=inset&&(scaled(at<float>(s.params,24))||rotated||cameraZoom!=1)?.5f:0;
    if(frame.rotated&&!s.specialUv)std::swap(insetX,insetY);
    std::vector<Vertex> vertices;auto add=[&](double left,double top,double right,double bottom,std::array<float,4> uv){int corners[]={0,1,2,2,1,3};for(int corner:corners){Vertex v;auto xy=m.apply(corner&1?right:left,corner>>1?bottom:top);v.position[0]=xy[0];v.position[1]=xy[1];auto col=vertexColor;if(s.gradient&&corner>>1){auto lower=color(s.lower);std::copy_n(lower.data(),3,col.data());}std::copy(col.begin(),col.end(),v.color);
            float xx=corner&1?uv[2]:uv[0],yy=corner>>1?uv[3]:uv[1];auto& rect=frame.rect;double u,vv;
            if(frame.rotated){u=(s.specialUv?rect[0]+uv[corner>>1?0:2]:rect[2]-yy)/frame.texture->width;vv=(rect[1]+(s.specialUv?uv[corner&1?3:1]:xx))/frame.texture->height;}else{u=(rect[0]+xx*(rect[2]<rect[0]?-1:1))/frame.texture->width;vv=(rect[1]+yy*(rect[3]<rect[1]?-1:1))/frame.texture->height;}
            if(directUv){
                double du=(corner&1?-1:1)*(frame.rotated?insetY:insetX)/frame.texture->width;
                double dv=(corner>>1?-1:1)*(frame.rotated?insetX:insetY)/frame.texture->height;
                if(frame.rotated){du=(corner>>1?1:-1)*insetX/frame.texture->width;dv=(corner&1?-1:1)*insetY/frame.texture->height;}
                v.texcoord[0][0]=float(u+du);v.texcoord[0][1]=float(vv+dv);v.texcoord[7][0]=v.texcoord[7][1]=1;
            }else{
                v.texcoord[0][0]=frame.rotated?1-yy/frame.height:xx/frame.width;v.texcoord[0][1]=frame.rotated?xx/frame.width:yy/frame.height;
                v.texcoord[6][0]=(rect[0]+insetX)/frame.texture->width;v.texcoord[6][1]=(rect[1]+insetY)/frame.texture->height;
                v.texcoord[7][0]=(rect[2]-rect[0]-2*insetX)/frame.texture->width;v.texcoord[7][1]=(rect[3]-rect[1]-2*insetY)/frame.texture->height;
            }
            v.texcoord[1][0]=s.font?float(s.channel):material.elapsed;v.texcoord[1][1]=material.row;v.texcoord[2][0]=1;
            for(int k=0;k<6;k++)v.texcoord[3+k/2][k%2]=material.parameters[k];vertices.push_back(v);}};
    add(0,0,frame.width,frame.height,source);
    bool stencil=state.phase!=0;
    unsigned stencilMode=state.phase==1?(state.invert?0:1):state.phase==2?(state.invert?3:2):0;bool writeColor=state.phase!=1||(state.channel!=0&&state.channel!=2);
    bool sourceHidden=(s.call!=SIZE_MAX&&currentImages_[s.call].hidden)||(s.actualCall!=SIZE_MAX&&currentImages_[s.actualCall].hidden);
    if(sourceHidden||dependencyHidden||(captureActive&&(s.offscreen?localHidden_:globalHidden_))){if(state.phase==1&&state.channel>=0)state.hiddenStencil[state.channel].insert(state.reference);return;}
    if(state.phase==2&&state.channel>=0&&state.hiddenStencil[state.channel].contains(state.reference))return;
    if(std::max(0,clip[0])>=std::min(output_.image.width,clip[2])||std::max(0,clip[1])>=std::min(output_.image.height,clip[3]))return;
    if(writeColor)markDrawn(s.actualCall==SIZE_MAX?s.call:s.actualCall);
    if(captureActive||stencil){float clear[4]{};gpu_.begin(working_,clear);auto unblended=material;unblended.blend=0x8000;gpu_.draw(vertices,unblended,clip);
        if(captureActive&&(flags==1||flags==2)){gpu_.begin(raw_,clear);auto raw=material;raw.program=s.font?2:4;raw.blend=0x8000;auto rawVerts=vertices;for(auto& v:rawVerts){v.color[0]=v.color[1]=v.color[2]=v.color[3]=1;v.texcoord[3][0]=1;}gpu_.draw(rawVerts,raw,clip);}
        gpu_.begin(destination);Material composite;composite.program=-1;composite.blend=flags;composite.textures[0]=gpu_.view(working_);composite.constants[0]=output_.image.width;composite.constants[1]=output_.image.height;
        if(captureActive){composite.textures[1]=gpu_.view(s.offscreen?localMask_:globalMask_);composite.textures[2]=gpu_.view(raw_);composite.constants[32]=flags==1||flags==2?1:(s.offscreen?localTarget_:globalTarget_)==2?2:3;std::copy(vertexColor.begin(),vertexColor.end(),composite.constants.begin()+36);}
        std::array<Vertex,6> full{};int corners[]={0,1,2,2,1,3};for(int n=0;n<6;n++){int corner=corners[n];full[n].position[0]=(corner&1)*output_.image.width;full[n].position[1]=(corner>>1)*output_.image.height;full[n].texcoord[2][0]=1;}
        gpu_.draw(full,composite,clip,stencilMode,state.reference,writeColor);if(stencil)statistics.stencilDraws++;
    }else {gpu_.begin(destination);gpu_.draw(vertices,material,clip);}
}
void Executor::capture(const Instruction& i,std::span<const uint8_t> payload,bool global,State& state){
    auto& active=global?globalCapture_:localCapture_;auto& target=global?globalMask_:localMask_;int& channel=global?globalTarget_:localTarget_;auto& maskHidden=global?globalHidden_:localHidden_;active=false;maskHidden=false;
    if(global){if(!payload[0])return;payload=payload.subspan(1);}else payload=payload.subspan(4);
    recordImage(replay_->path(i.resource),i.frame,state.layer,"mask",true);maskHidden=hidden(replay_->path(i.resource));
    Sprite s;s.frame=assets_.frame(replay_->path(i.resource),i.frame);if(!s.frame||s.frame->empty)return;if(!s.frame->actualPath.empty()&&(s.frame->actualPath!=imagePath(replay_->path(i.resource))||s.frame->actualFrame!=i.frame)){recordImage(s.frame->actualPath,s.frame->actualFrame,state.layer,"mask-placeholder",true);maskHidden|=hidden(s.frame->actualPath);}
    int scaleOffset=global?8:4,positionOffset=global?16:12,pivotOffset=global?24:20,rotationOffset=global?32:28;
    s.x=global?float(at<int32_t>(payload,positionOffset)):at<float>(payload,positionOffset);s.y=global?float(at<int32_t>(payload,positionOffset+4)):at<float>(payload,positionOffset+4);
    put(s.params,20,at<float>(payload,scaleOffset));put(s.params,24,at<float>(payload,scaleOffset+4));put(s.params,16,at<float>(payload,rotationOffset));float px=at<float>(payload,pivotOffset),py=at<float>(payload,pivotOffset+4);put(s.params,28,px>1e30?-s.x:px);put(s.params,32,py>1e30?-s.y:py);
    if(!global){s.params[6]=payload[32];channel=payload[33];}else channel=0;
    auto matrix=affine(s,*s.frame);if(std::abs(matrix.a*matrix.d-matrix.b*matrix.c)<1e-12)return;Material material=binder_.bind(nullptr,gpu_.texture(s.frame->texture),{});material.blend=0x8000;
    std::array<Vertex,6> vertices{};int corners[]={0,1,2,2,1,3};auto& f=*s.frame;for(int n=0;n<6;n++){int corner=corners[n];auto& v=vertices[n];auto xy=matrix.apply((corner&1)*f.width,(corner>>1)*f.height);v.position[0]=xy[0];v.position[1]=xy[1];if(f.rotated){v.texcoord[0][0]=float(f.rect[2]-(corner>>1)*f.height)/f.texture->width;v.texcoord[0][1]=float(f.rect[1]+(corner&1)*f.width)/f.texture->height;}else{v.texcoord[0][0]=float(f.rect[corner&1?2:0])/f.texture->width;v.texcoord[0][1]=float(f.rect[corner>>1?3:1])/f.texture->height;}v.texcoord[2][0]=1;v.texcoord[7][0]=v.texcoord[7][1]=1;}
    float clear[4]{};gpu_.begin(target,clear);gpu_.draw(vertices,material,{0,0,output_.image.width,output_.image.height});gpu_.begin(renderTarget(state.context));active=true;
}
void Executor::execute(const Scene& scene){
    if(!replay_)throw Error("executor has no replay");sceneEntryCameras_=cameras_;sceneEntryOffsets_=samplerOffsets_;sceneEntryBlend_=lastBlend_;sceneEntryRandom_=binder_.randomState();sceneEntryValid_=true;currentImages_.clear();durationMilliseconds_=std::max(durationMilliseconds_,scene.timestamp);State state;state.clip={0,0,output_.image.width,output_.image.height};auto initialize=[&](State& s){s.context=replay_->header.renderMode==2?(replay_->version>=17||replay_->version<=11?82:10):(replay_->version>=17?81:10);};initialize(state);
    std::map<size_t,std::vector<GlyphQuad>> textQuads;size_t cursor=0;Reader aux(scene.aux);
    for(auto id:scene.ids)if(auto command=replay_->command(id))for(const auto& i:command->instructions){auto payload=std::span(command->raw).subspan(i.offset,i.payloadBytes);float x=0,y=0;if(i.auxBytes){x=aux.get<int16_t>();y=aux.get<int16_t>();}
        if(i.opcode==16||i.opcode==17)layer(state,i,payload,*replay_);
        contextState(state,i);
        if(i.opcode==50){Camera camera{at<float>(payload,0),at<float>(payload,4),at<float>(payload,12),at<float>(payload,16),at<float>(payload,20)};uint32_t target=at<uint32_t>(payload,8);
            if(target==207){float clear[4]{};for(int n=0;n<12;n++){camera.x=std::clamp(camera.x,-1e6f,1e6f);camera.y=std::clamp(camera.y,-1e6f,1e6f);actorCameras_[n]=camera;if(!actors_[n])actors_[n]=gpu_.target(output_.image.width,output_.image.height,false);gpu_.begin(*actors_[n],clear);}statistics.actorPoolUpdates+=12;}
            else {cameras_[{cameraId(state.context,target),target}]=camera;statistics.cameraUpdates++;}}
        if(i.opcode==5||i.opcode==48){if(i.opcode==48&&replay_->header.minor<7){x=at<float>(i.native,44);y=at<float>(i.native,48);}textQuads[cursor]=fonts_.prepare(i,*replay_,x,y);}cursor++;
    }
    aux.end();state={};state.clip={0,0,output_.image.width,output_.image.height};initialize(state);statistics.lastContext=state.context;cursor=0;Reader data(scene.aux);localCapture_=globalCapture_=localHidden_=globalHidden_=false;float black[4]={0,0,0,transparent_?0.f:1.f};gpu_.begin(output_,black);std::array<bool,12> activated{};
    std::set<const Pixels*> updatedFonts;
    for(auto id:scene.ids){statistics.references++;auto command=replay_->command(id);if(!command)continue;
        for(const auto& i:command->instructions){auto payload=std::span(command->raw).subspan(i.offset,i.payloadBytes);float x=0,y=0;if(i.auxBytes){x=data.get<int16_t>();y=data.get<int16_t>();}statistics.opcodes[i.opcode]++;
            auto sprite=[&](){Sprite s;s.instruction=&i;s.opcode=i.opcode;s.resource=i.resource;s.frameIndex=i.frame;s.params=i.params;s.x=x;s.y=y;s.layer=state.layer;s.context=state.context;if(i.opcode>=42&&i.opcode<=47){s.layer=i.layer;s.explicitCamera=true;s.offscreen=i.opcode>=45;int mode=(i.opcode-42)%3;if(mode<2){s.opcode=i.native[40]?3:4;s.params[6]=i.native[41];s.params[12]=i.native[42];s.bypassZoom=i.native[mode==1?56:44]!=0;if(mode==1){put(s.params,48,at<float>(i.native,44));put(s.params,52,at<float>(i.native,48));put(s.params,56,at<float>(i.native,52));}}}return s;};
            switch(i.opcode){
            case 0:{auto type=at<uint32_t>(payload,0)&255;if(type!=7)state.blends.push_back({type,at<uint32_t>(payload,4)});break;}
            case 1:if(!state.blends.empty())state.blends.pop_back();break;
            case 2:for(int n=0;n<4;n++)state.clip[n]=at<int16_t>(payload,n*2);state.worldClip=true;state.nextClip=false;break;
            case 3:case 4:case 42:case 43:case 44:case 45:case 46:case 47:draw(sprite(),state);break;
            case 5:case 48:{auto glyphs=textQuads.find(cursor);if(glyphs!=textQuads.end())for(auto& glyph:glyphs->second){Sprite s;s.opcode=3;s.frame=glyph.frame;s.x=glyph.x;s.y=glyph.y;s.font=true;s.channel=glyph.channel;s.layer=i.opcode==48?i.layer:state.layer;s.context=state.context;s.explicitCamera=i.opcode==48;s.gradient=glyph.gradient;s.lower=glyph.lower;
                put(s.params,28,0.f);put(s.params,32,0.f);put(s.params,8,glyph.color);put(s.params,20,glyph.scale*(glyph.frame->rect[2]<glyph.frame->rect[0]?-1:1));put(s.params,24,glyph.scale*(glyph.frame->rect[3]<glyph.frame->rect[1]?-1:1));s.update=updatedFonts.insert(glyph.frame->texture.get()).second;draw(std::move(s),state);}
                if(!state.effects.empty()&&state.effects.back()->type==30){auto effects=std::move(state.effects);state.effects.clear();if(glyphs!=textQuads.end())for(auto& glyph:glyphs->second){Sprite s;s.frame=glyph.frame;s.x=glyph.x;s.y=glyph.y;s.font=true;s.channel=glyph.channel;s.layer=i.opcode==48?i.layer:state.layer;s.context=state.context;s.explicitCamera=i.opcode==48;s.gradient=glyph.gradient;s.lower=glyph.lower;put(s.params,28,0.f);put(s.params,32,0.f);put(s.params,8,glyph.color);put(s.params,20,glyph.scale*(glyph.frame->rect[2]<glyph.frame->rect[0]?-1:1));put(s.params,24,glyph.scale*(glyph.frame->rect[3]<glyph.frame->rect[1]?-1:1));draw(s,state);}state.effects=std::move(effects);}break;}
            case 6:case 7:case 41:statistics.audioEvents++;break;
            case 10:for(auto instance:i.instances){auto s=sprite();s.frameIndex=instance[1];s.x+=instance[2];s.y+=instance[3];put(s.params,4,int16_t(instance[1]));draw(std::move(s),state);}break;
            case 12:state.nextClip=true;break;
            case 15:capture(i,payload,true,state);break;
            case 16:case 17:layer(state,i,payload,*replay_);break;
            case 18:case 64:if(!payload[4]){auto s=sprite();s.params=drawDefaults();s.x=at<int16_t>(payload,0);s.y=at<int16_t>(payload,2);s.frameIndex=payload[5];s.layer=i.opcode==64?at<uint32_t>(payload,8):state.layer;s.explicitCamera=i.opcode==64;draw(std::move(s),state);}break;
            case 19:if(i.effect.present&&!i.effect.obsolete){if(i.effect.type<67)statistics.effects[i.effect.type]++;if(i.effect.programNull()){state.phantom++;statistics.phantomPushes++;}else {State::EffectBinding binding{&i.effect,{}};if(replay_->options.profile==ProtocolProfile::DnfJuly2026&&i.effect.type==54&&i.effect.floats.size()==7&&i.effect.floats[6]!=0){int slot=selectedContext_-219;if(contextBound_[slot]&&contexts_[slot])binding.mask=gpu_.view(*contexts_[slot]);}state.effects.push_back(std::move(binding));}}break;
            case 20:if(state.phantom)state.phantom--;else if(!state.effects.empty())state.effects.pop_back();break;
            case 21:statistics.nullCaches++;break;
            case 23:{auto instance=at<uint64_t>(payload,8);auto timestamp=at<uint32_t>(payload,36);auto frame=movies_->frame(replay_->path(i.resource),instance,timestamp);if(!frame)break;auto s=sprite();s.opcode=3;s.params=drawDefaults();s.frame=frame;s.x=at<int32_t>(payload,16);s.y=at<int32_t>(payload,20);s.layer=i.layer;s.explicitCamera=replay_->version>=17;s.update=true;put(s.params,28,0.f);put(s.params,32,0.f);put(s.params,20,at<float>(payload,24));put(s.params,24,at<float>(payload,28));put(s.params,8,movies_->color(instance,at<uint32_t>(payload,32),replay_->version>=17));auto oldClip=state.clip;std::array<int,4> movieClip;bool any=false;for(int n=0;n<4;n++){movieClip[n]=at<int16_t>(payload,40+n*2);any|=movieClip[n]!=0;}if(any){for(int n=0;n<4;n++)state.clip[n]=n<2?std::max(oldClip[n],movieClip[n]):std::min(oldClip[n],movieClip[n]);if(state.clip[0]>=state.clip[2]||state.clip[1]>=state.clip[3])state.clip={0,0,output_.image.width,output_.image.height};}draw(std::move(s),state);state.clip=oldClip;break;}
            case 24:{auto id=at<uint64_t>(payload,8);if(auto pixels=movies_->pixels(id))gpu_.forgetTexture(pixels.get());movies_->stop(id);break;}
            case 26:case 27:case 28:case 29:{int channel=payload[0];if(channel>=3)break;if(i.opcode==26){state.channel=channel;state.phase=1;state.nextStencil[channel]=std::array{1,64,128}[channel];if(channel==0||channel==2){gpu_.clearStencil(renderTarget(state.context));for(auto& hiddenRefs:state.hiddenStencil)hiddenRefs.clear();}}else if(i.opcode==27){state.channel=channel;state.phase=2;state.reference=std::array{1,64,128}[channel];}else if(i.opcode==28){state.invert=payload[1]!=0;if(state.phase==1){state.reference=state.nextStencil[channel]-int(state.invert);if(!state.invert)state.nextStencil[channel]++;}else state.reference=payload[2];}else{state.channel=-1;state.phase=0;}break;}
            case 32:{Reader grid(payload);grid.take(4);State::Grid value;auto count=grid.get<uint8_t>();while(count--)value.xs.push_back(grid.get<int16_t>());count=grid.get<uint8_t>();while(count--)value.ys.push_back(grid.get<int16_t>());state.grids[{i.resource,i.frame}]=std::move(value);break;}
            case 33:state.grids.erase({i.resource,i.frame});break;
            case 38:if(i.nativeContextState){contextState(state,i);selectedContext_=state.context;int slot=state.context-219;if(!contexts_[slot])contexts_[slot]=gpu_.target(output_.image.width,output_.image.height);float clear[4]{};gpu_.begin(*contexts_[slot],activated[slot]?nullptr:clear);activated[slot]=true;statistics.contextAllocations++;}break;
            case 39:if(i.nativeContextState){contextState(state,i);contextBound_[selectedContext_-219]=true;gpu_.begin(renderTarget(state.context));statistics.contextBindings++;}break;
            case 40:if(i.nativeContextState){if(!state.effects.empty())state.effects.pop_back();statistics.contextReleases++;}break;
            case 51:state.flagsStack.push_back(state.flags);state.flags=at<uint16_t>(payload,0);break;
            case 52:state.flags=state.flagsStack.empty()?0:state.flagsStack.back();if(!state.flagsStack.empty())state.flagsStack.pop_back();break;
            case 53:state.colorMode=at<uint16_t>(payload,0);state.color=at<uint32_t>(payload,2);break;
            case 57:{auto s=sprite();s.params=drawDefaults();s.opcode=3;s.layer=i.layer;s.explicitCamera=true;s.directGridExtent=replay_->version>=18;s.x=at<float>(i.native,8);s.y=at<float>(i.native,12);put(s.params,20,at<float>(i.native,16));put(s.params,24,at<float>(i.native,20));put(s.params,16,at<float>(i.native,24));put(s.params,8,at<uint32_t>(i.native,28));put(s.params,52,at<float>(i.native,32));put(s.params,56,at<float>(i.native,36));Reader arrays(payload.subspan(4+(replay_->version<18?32:40)));auto nx=arrays.get<uint64_t>(),ny=arrays.get<uint64_t>();State::Grid value;while(nx--)value.xs.push_back(arrays.get<int32_t>());while(ny--)value.ys.push_back(arrays.get<int32_t>());s.grid=value;draw(std::move(s),state);break;}
            case 58:capture(i,payload,false,state);break;
            case 59:localCapture_=false;break;
            case 60:for(int n=0;n<4;n++)state.clip[n]=at<int32_t>(payload,n*4);state.worldClip=true;break;
            case 61:{state.samplers.push_back(state.sampler);State::Sampler s;s.number=payload[0];s.flag=payload[1];s.sx=at<float>(payload,8);s.sy=at<float>(payload,12);s.x=at<float>(payload,16);s.y=at<float>(payload,20);s.ox=at<float>(payload,24);s.oy=at<float>(payload,28);state.sampler=s;break;}
            case 62:state.sampler=state.samplers.empty()?std::nullopt:state.samplers.back();if(!state.samplers.empty())state.samplers.pop_back();break;
            case 63:state.samplerEnabled=payload[0]!=0;break;
            default:break;
            }statistics.lastContext=state.context;cursor++;
        }
    }
    data.end();if(!state.blends.empty()||!state.effects.empty()||state.phantom)throw Error("unbalanced scene rendering state at "+std::to_string(scene.timestamp));statistics.scenes++;
}
void Executor::redraw(const Scene& scene){if(sceneEntryValid_){cameras_=sceneEntryCameras_;samplerOffsets_=sceneEntryOffsets_;lastBlend_=sceneEntryBlend_;binder_.restoreRandomState(sceneEntryRandom_);}execute(scene);}
void Playback::attach(Replay& replay,Executor& executor){
    replay_=&replay;executor_=&executor;hasScene_=false;ended_=false;stopped_=true;paused_=false;sequentialState_=true;elapsed_=0;selected=skipped=0;contextDependent_=false;
    for(const auto& [id,command]:replay.dictionary)for(const auto& i:command.instructions)
        if(i.nativeContextState&&(i.opcode==38||i.opcode==39))contextDependent_=true;
}
void Playback::start(){if(!replay_)return;replay_->rewind();executor_->resetPlaybackState();hasScene_=ended_=paused_=false;stopped_=false;sequentialState_=true;elapsed_=0;selected=skipped=0;start_=std::chrono::steady_clock::now();}
int64_t Playback::elapsedMilliseconds()const{if(stopped_||paused_||ended_)return elapsed_;return std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start_).count());}
void Playback::stop(){elapsed_=elapsedMilliseconds();stopped_=true;paused_=false;}
void Playback::pause(){if(!playing())return;elapsed_=elapsedMilliseconds();paused_=true;}
void Playback::resume(){if(!replay_||stopped_||ended_||!paused_)return;start_=std::chrono::steady_clock::now()-std::chrono::milliseconds(elapsed_);paused_=false;}
bool Playback::refresh(){
    if(!hasScene_||!executor_)return false;
    // Context textures may be overwritten later in the same scene. Rebuild
    // their dependencies instead of retaining copies of all twelve targets.
    if(contextDependent_){bool wasEnded=ended_;bool changed=rebuildToOrdinal(scene_.ordinal);ended_=wasEnded;return changed;}
    executor_->redraw(scene_);return true;
}
bool Playback::seek(int64_t elapsed){if(!replay_)return false;bool wasPaused=paused_,wasStopped=stopped_;replay_->rewind();executor_->resetPlaybackState();hasScene_=ended_=false;stopped_=false;paused_=false;sequentialState_=true;elapsed=std::max<int64_t>(0,elapsed);Scene next;bool changed=false;
    while(!hasScene_||scene_.timestamp<elapsed){if(!replay_->next(next)){ended_=true;break;}scene_=std::move(next);hasScene_=true;executor_->execute(scene_);selected++;changed=true;}
    elapsed_=ended_?scene_.timestamp:elapsed;start_=std::chrono::steady_clock::now()-std::chrono::milliseconds(elapsed_);paused_=wasPaused;stopped_=wasStopped;return changed;}
bool Playback::rebuildToOrdinal(uint64_t ordinal){
    replay_->rewind();executor_->resetPlaybackState();hasScene_=ended_=false;sequentialState_=true;Scene next;
    while(replay_->next(next)){scene_=std::move(next);hasScene_=true;executor_->execute(scene_);selected++;if(scene_.ordinal>=ordinal)return true;}
    ended_=true;if(!hasScene_)scene_={};return false;
}
bool Playback::step(int direction){
    if(!direction||!replay_||!executor_)return false;stopped_=false;paused_=true;bool changed=false;
    if(!hasScene_)changed=rebuildToOrdinal(0);
    else if(direction<0){if(scene_.ordinal>0)changed=rebuildToOrdinal(scene_.ordinal-1);}
    else{Scene next;if(replay_->next(next)){
        if(!sequentialState_)changed=rebuildToOrdinal(next.ordinal);
        else{scene_=std::move(next);executor_->execute(scene_);selected++;ended_=false;changed=true;}
    }else{if(!sequentialState_)rebuildToOrdinal(scene_.ordinal);ended_=true;}}
    elapsed_=hasScene_?std::max<int64_t>(0,scene_.timestamp):0;start_=std::chrono::steady_clock::now()-std::chrono::milliseconds(elapsed_);return changed;
}
bool Playback::select(int64_t elapsed){
    if(stopped_||ended_||!replay_)return false;if(hasScene_&&scene_.timestamp>=elapsed)return false;
    bool changed=false;Scene next;while(!hasScene_||scene_.timestamp<elapsed){if(!replay_->next(next)){ended_=true;break;}if(changed){skipped++;if(!contextDependent_)sequentialState_=false;}scene_=std::move(next);hasScene_=true;changed=true;
        if(contextDependent_){executor_->execute(scene_);selected++;}}
    elapsed_=ended_?scene_.timestamp:std::max<int64_t>(0,elapsed);if(changed&&!contextDependent_){executor_->execute(scene_);selected++;}return changed;
}
bool Playback::tick(){if(paused_)return false;return select(elapsedMilliseconds());}
}
