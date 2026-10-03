#include "bindings.hpp"
#include "shader_registry.hpp"
#include "preset_table.hpp"
#include <algorithm>
#include <cmath>
#include <bit>
#include <functional>

namespace rep {
int programId(std::string_view name){for(const auto& r:shaderRecords)if(name==r.name)return r.id;throw Error("unknown original technique: "+std::string(name));}
std::vector<ImageInput> shaderImageInputs(const Effect* e,const Replay& replay){
    std::vector<ImageInput> result;if(!e||!e->present||e->obsolete||e->programNull())return result;const auto& f=e->floats;
    auto builtin=[&](int frame,int group=0){static const char* names[]={"mask","dissolvemask","flametexture","glitchtexture","waterdistortion","delezieeffecttexture"};if(frame>=0&&group>=0&&group<6)result.push_back({std::string("sprite/shader/")+names[group]+".img",frame});};
    int mode=e->bits.size()>=6?int(std::bit_cast<float>(e->bits[5])):0;
    switch(e->type){
    case 12:if(f.size()>=29)builtin(int(f[25]),1);break;
    case 13:if(mode==1&&f.size()>=17)builtin(6);break;
    case 15:if(mode==1&&f.size()>=27)builtin(5);break;
    case 16:if(!f.empty()){
        unsigned flags=unsigned(int(f[0]))&65535;size_t cursor=1;std::map<unsigned,std::vector<float>> groups;bool modern=replay.version>14||(replay.version==14&&replay.header.minor>=2);int bits[]={1,16,128,256,64,2,4,8},sizes[]={9,17,17,17,17,16,modern?27:25,modern?29:26};
        for(int n=0;n<8;n++)if(flags&bits[n]){if(f.size()<cursor+sizes[n])return result;groups[bits[n]]={f.begin()+cursor,f.begin()+cursor+sizes[n]};cursor+=sizes[n];}
        int frame=-1,group=0;if(groups.contains(8)){frame=int(groups[8][25]);group=1;}
        for(auto [bit,value]:{std::pair{16,2},std::pair{128,3},std::pair{256,5}})if(groups.contains(bit)){frame=int(groups[bit][8]);frame=frame>=0?frame:~frame;group=value;}
        if((flags&0x1d8)&&frame!=-1)builtin(frame,group);
    }break;
    case 17:if(f.size()>=25){builtin(4);if(int(f[10])!=-1)builtin(int(f[10]));}break;
    case 24:if(f.size()>=13)builtin(3);break;
    case 36:if(e->bits.size()==24)for(int index:{20,22}){int id=int(std::bit_cast<float>(e->bits[index])),frame=int(std::bit_cast<float>(e->bits[index+1]));auto path=replay.path(id);if(!path.empty()&&frame>0)result.push_back({path,frame});}break;
    case 38:if(f.size()>=25){builtin(std::max(0,int(f[20])),int(f[19])>=0&&int(f[19])<6?int(f[19]):0);builtin(std::max(0,int(f[23])),int(f[22])>=0&&int(f[22])<6?int(f[22]):0);}break;
    case 39:if(f.size()>=14)builtin(std::max(0,int(f[9])),int(f[8])>=0&&int(f[8])<6?int(f[8]):0);break;
    case 40:if(f.size()>=17)builtin(0,3);break;
    case 47:if(f.size()>=11)builtin(4);break;
    case 49:if(f.size()>=3)builtin(0,4);break;
    case 52:if(f.size()>=3)builtin(0,4);break;
    default:break;
    }return result;
}
std::shared_ptr<Frame> Binder::builtin(int frame,int group) {
    const char* names[]={"mask","dissolvemask","flametexture","glitchtexture","waterdistortion","delezieeffecttexture"};
    if(group<0||group>=6)throw Error("invalid builtin shader texture group");
    uint64_t key=(uint64_t(group)<<32)|uint32_t(frame);if(auto it=builtin_.find(key);it!=builtin_.end()){auto path=std::string("sprite/shader/")+names[group]+".img";assets_.notifyFrame(path,frame);if(it->second->actualPath!=path&&!it->second->actualPath.empty())assets_.notifyFrame(it->second->actualPath,it->second->actualFrame);return it->second;}
    return builtin_[key]=assets_.frame(std::string("sprite/shader/")+names[group]+".img",frame);
}
static std::string staticName(unsigned flags) {
    std::string stem;if(flags&1)stem=(flags&2)?"MetalGlow":"Metal";else if(flags&4)stem=(flags&2)?"ShinyGlow":"Shiny";
    else if(flags&2)return (flags&64)?"StaticBranch_Glow_Gradation_Outline":"StaticBranch_ColorGlow";
    std::string suffix;if(flags&8)suffix="Dissolve";else if(flags&0x190)suffix=(flags&256)?"Outline2":"Outline";
    return "StaticBranch_"+(stem+suffix==""?"Draw":stem+suffix);
}
Material Binder::bind(const Effect* e,std::shared_ptr<GpuTexture> source,ShaderContext c) {
    auto create=[&](){auto observer=assets_.frameObserver();std::vector<ImageInput> inputs;assets_.setFrameObserver([&](const std::string& path,int frame){inputs.push_back({path,frame});if(observer)observer(path,frame);});
        try{auto material=bindUncached(e,source,c);assets_.setFrameObserver(std::move(observer));material.inputs=std::move(inputs);return material;}
        catch(...){assets_.setFrameObserver(std::move(observer));throw;}};
    if(e&&(e->type==16||e->type==17||e->type==57)){
        CacheKey key{e,source.get(),c.width,c.height,c.channel,c.direction,c.directSource};auto it=materials_.find(key);if(it!=materials_.end()){for(auto& input:it->second.inputs)assets_.notifyFrame(input.path,input.frame);return it->second;}
        auto material=create();materials_[key]=material;return material;
    }
    return create();
}
Material Binder::bindUncached(const Effect* e,std::shared_ptr<GpuTexture> source,ShaderContext c) {
    Material m;m.program=c.channel?2:4;m.textures[0]=source;m.elapsed=float(c.channel);
    for(int n=0;n<4;n++){m.constants[n*4]=float(source->width);m.constants[n*4+1]=float(source->height);m.constants[n*4+2]=1-.5f/source->width;m.constants[n*4+3]=1-.5f/source->height;}
    for(int n=4;n<=6;n++){m.constants[n*4+2]=m.constants[n*4+3]=1;}
    if(c.directSource){m.constants[2]=float(source->logicalWidth?source->logicalWidth:source->width)/source->width-.5f/source->width;m.constants[3]=float(source->logicalHeight?source->logicalHeight:source->height)/source->height-.5f/source->height;}
    if(!e||!e->present||e->obsolete||e->programNull())return m;
    const auto& f=e->floats;unsigned t=e->type;int mode=e->bits.size()>=6?int(std::bit_cast<float>(e->bits[5])):0;
    auto require=[&](size_t n){if(f.size()<n)throw Error("shader "+std::to_string(t)+" requires "+std::to_string(n)+" floats");};
    auto params=[&](std::initializer_list<float> values){std::copy(values.begin(),values.end(),m.parameters.begin());};
    auto range=[&](size_t n,size_t size){for(size_t k=0;k<size&&k<6;k++)m.parameters[k]=f[n+k];};
    auto extra=[&](std::initializer_list<float> values){std::copy(values.begin(),values.end(),m.constants.begin()+32);};
    auto extraRange=[&](size_t start,size_t count){std::copy(f.begin()+start,f.begin()+start+count,m.constants.begin()+32);};
    auto frameTexture=[&](int slot,std::shared_ptr<Frame> frame){auto texture=gpu_.texture(frame->texture);m.textures[slot]=texture;
        int start=16+4*(slot-1);m.constants[start]=float(frame->rect[0])/texture->width;m.constants[start+1]=float(frame->rect[1])/texture->height;
        m.constants[start+2]=float(frame->rect[2]-frame->rect[0])/texture->width;m.constants[start+3]=float(frame->rect[3]-frame->rect[1])/texture->height;
        if(frame->rotated)std::swap(m.constants[start+2],m.constants[start+3]);
        m.constants[slot*4+2]=1-.5f/texture->width;m.constants[slot*4+3]=1-.5f/texture->height;};
    auto tex=[&](int slot,int frame,int group=0){frameTexture(slot,builtin(frame,group));};
    auto canvas=[&](){if(!c.canvas)throw Error("shader requires captured replay target");m.textures[1]=c.canvas;m.constants[4]=c.canvas->width;m.constants[5]=c.canvas->height;};
    std::array<float,256> dynamic{};
    auto scalar=[&](int index,float v,bool signedValue=true){if(v<0&&signedValue){dynamic[index*4]=std::abs(v)/std::exp2(float(.1*332.19281));dynamic[index*4+3]=.2f;}else dynamic[index*4]=v;};
    auto color=[&](int index,const float* values,float divisor=1.f){for(int k=0;k<4;k++)dynamic[index*4+k]=values[k]/divisor;};
    static const std::pair<unsigned,int> simple[]={ {0,67},{1,100},{2,99},{3,47},{4,56},{20,49},{25,73},{26,68},{5,86},{10,98},{23,90},{27,43},{28,75},{37,93},{50,94},{51,95},{53,77},{59,107},{60,106},{61,113},{65,153}};
    for(auto [type,id]:simple)if(t==type){m.program=id;if(t==0)m.parameters[0]=1;
        if(t==4||t==5){canvas();int offset=t==5?0:2;m.parameters[offset]=c.canvas->width;m.parameters[offset+1]=c.canvas->height;}
        for(size_t k=0;k<std::min(size_t(6),f.size());k++)m.parameters[k]=f[k];return m;}
    switch(t) {
    case 6:require(17);m.program=programId("spriteEffect_glitch");range(10,2);extraRange(0,10);m.constants[42]=6.28310013f;m.constants[43]=50;break;
    case 11:if(mode==0){require(2);m.program=programId("spriteEffect_HS");range(0,2);}else if(mode==1){require(4);m.program=programId("spriteEffect_ExceptColor");range(0,4);}else throw Error("invalid HS mode");break;
    case 12:require(29);m.program=programId("Sprite_Dissolve");params({f[9],f[10],f[23]*f[24],f[17],f[18],f[19]});extra({f[21],float(bool(f[22])),float(bool(f[26])),0});tex(1,int(f[25]),1);break;
    case 13:require(17);m.elapsed=f[2];extraRange(3,4);if(mode==0){m.program=programId("Sprite_Metal");m.parameters[0]=1;}else if(mode==1){m.program=programId("Sprite_Mask_Metal");params({1,0,f[11],f[12],f[13],f[14]});tex(1,6);}else throw Error("invalid Metal mode");break;
    case 14:require(16);m.program=programId("Sprite_Glow");extraRange(12,4);params({1,0,f[0],f[1],f[2],f[3]});break;
    case 29:require(2);m.program=programId("sprite_MaskBlur");canvas();params({float(c.canvas->width),float(c.canvas->height),f[0],f[1],0,0});break;
    case 15:{require(27);const char* names[]={"Sprite_Shiny","Sprite_OnlyShiny_with_maskTex","Sprite_OnlyShiny_Overlay","Sprite_OnlyShiny"};if(mode<0||mode>=4)throw Error("invalid Shiny mode");m.program=programId(names[mode]);params({f[3],f[2],f[14],f[15],f[16],f[17]});extra({f[8],f[7],0,0,f[10],f[11],f[12],f[13]});if(mode==1)tex(1,5);break;}
    case 16:{
        require(1);unsigned flags=unsigned(int(f[0]))&65535;std::unordered_map<int,std::vector<float>> groups;size_t cursor=1;
        bool modern=c.version>14||(c.version==14&&c.minor>=2);int bits[]={1,16,128,256,64,2,4,8},sizes[]={9,17,17,17,17,16,modern?27:25,modern?29:26};
        for(int n=0;n<8;n++)if(flags&bits[n]){require(cursor+sizes[n]);groups[bits[n]]={f.begin()+cursor,f.begin()+cursor+sizes[n]};cursor+=sizes[n];if(bits[n]==4)groups[4].resize(27);if(bits[n]==8)groups[8].resize(29);}
        auto name=staticName(flags);
        // 146F7FB30 resolves selector 116 through the builtin draw program (4),
        // including effects without a preset tail. Its UVs retain IMG padding/atlas mapping.
        m.program=name=="StaticBranch_Draw"?4:programId(name);params({float(c.direction),0,0,0,1,1});
        if(groups.contains(1)){auto& g=groups[1];scalar(2,g[0]);color(3,g.data()+3);m.elapsed=g[1];}
        if(groups.contains(2)){auto& g=groups[2];scalar(4,g[1]);scalar(5,g[6]);scalar(6,g[7]);scalar(7,g[8]);color(8,g.data()+12);m.elapsed=g[10];}
        for(int bit:{16,128,256,64})if(groups.contains(bit)){auto& g=groups[bit];scalar(9,g[1]);scalar(10,g[2]);color(11,g.data()+4);scalar(12,g[11]);scalar(13,g[12]);break;}
        if(groups.contains(4)){auto& g=groups[4];scalar(15,g[4]);scalar(16,g[7]);scalar(17,g[8]);scalar(18,g[9]);color(19,g.data()+10);std::copy(g.begin()+14,g.begin()+18,m.parameters.begin()+2);m.elapsed=g[24];}
        int textureFrame=-1,textureGroup=0;
        if(groups.contains(8)){auto& g=groups[8];scalar(20,g[11]);scalar(21,g[12]);scalar(22,g[21]);scalar(23,float(bool(g[22])));scalar(24,g[17]);scalar(25,g[18]);scalar(26,g[19]);m.parameters[1]=g[23]*g[24];textureFrame=int(g[25]);textureGroup=1;}
        for(auto [bit,group]:{std::pair{16,2},std::pair{128,3},std::pair{256,5}})if(groups.contains(bit)){int frame=int(groups[bit][8]);textureFrame=frame>=0?frame:~frame;textureGroup=group;}
        if(f.size()>cursor+1){
            if(name=="StaticBranch_Draw"||name=="StaticBranch_Glow_Gradation_Outline"){m.program=4;dynamic.fill(0);}
            else {
                int grade=int(f[cursor]),glow=int(f[cursor+1]);int index=(flags&5)?std::clamp(grade-1,0,(flags&1)?19:7):0;
                if(flags&2)index=(flags&64)?0:5*index+glow;if(flags&16)index*=3;else if(flags&128)index=3*index+1;else if(flags&256)index=3*index+2;
                bool dissolve=name.find("Dissolve")!=std::string::npos,outline=name.find("Outline")!=std::string::npos;
                if(dissolve){auto& g=groups.at(8);int variant=(g[21]==preset_dissolve[12]?8:0)+(g[21]==preset_dissolve[13]?4:0)+((int(g[22])&255)==1?2:0)+(g[17]!=1||g[18]!=1||g[19]!=1||g[20]!=1);index=12*index+variant;}
                int count=dissolve?12:outline?3:1,baseIndex=index/count,variant=index%count;bool metal=name.find("Metal")!=std::string::npos,shiny=name.find("Shiny")!=std::string::npos,hasGlow=name.find("Glow")!=std::string::npos;
                if(hasGlow){glow=baseIndex%5;baseIndex/=5;}else glow=0;
                if(baseIndex<0||baseIndex>=(metal?20:shiny?8:1)||glow<0||glow>4)throw Error("StaticBranch preset index out of range");
                dynamic.fill(0);const float* base=metal?preset_metal[baseIndex]:shiny?preset_shiny[baseIndex]:nullptr;
                if(metal){scalar(2,base[4]);color(3,base,255);}else if(shiny){scalar(15,base[7]);scalar(16,base[4]);scalar(17,base[5]);scalar(18,base[6]);color(19,base,255);}
                if(hasGlow){const float* g=glow<4?preset_glow[glow]:base?base+(metal?5:8):nullptr;float zero[8]{};if(!g)g=zero;scalar(4,g[4]);scalar(5,g[7]);scalar(6,g[5]);scalar(7,g[6]);color(8,g,255);}
                if(outline){const float* h=variant==0?preset_bakal:variant==1?preset_artificialgod:preset_delezie;scalar(9,h[1]);scalar(10,h[2]);color(11,h+11,255);scalar(12,h[9]);scalar(13,h[10]);}
                if(dissolve){auto d=preset_dissolve;scalar(20,d[6]);scalar(21,d[7]);scalar(22,variant/4==0?0:variant/4==1?d[13]:d[12]);scalar(23,float((variant/2)%2));scalar(24,variant%2?d[8]:1);scalar(25,variant%2?d[9]:1);scalar(26,variant%2?d[10]:1);}
            }
        }
        extra({1.f/64,.5f/64});bool external=name.find("Dissolve")!=std::string::npos||name.find("Outline")!=std::string::npos;
        if(external){if(textureFrame!=-1)tex(1,textureFrame,textureGroup);m.textures[2]=gpu_.floatTexture(dynamic,64,1);}else m.textures[1]=gpu_.floatTexture(dynamic,64,1);break;
    }
    case 17:require(25);m.program=programId(mode==1?"Sprite_Derangement_LinearDodge":"Sprite_Derangement");params({f[11],f[12],f[3]*.001f*f[0],f[3]*.001f*f[1],1,f[18]*.000062831852f});m.elapsed=f[3];extra({1.f/64,.5f/64});scalar(2,f[10],false);scalar(3,f[4],false);scalar(4,f[9],false);color(5,f.data()+5);color(6,f.data()+19);tex(1,4);if(int(f[10])!=-1)tex(2,int(f[10]));else m.textures[2]=source;m.textures[3]=gpu_.floatTexture(dynamic,64,1);break;
    case 57:require(12);m.program=programId("spriteEffect_HSL_Colourize_Haze");params({f[3],f[4],f[5],f[7],f[8],f[9]?0.f:1.f});m.elapsed=f[6];extra({1.f/64,.5f/64,3.1415f});scalar(2,f[0]);scalar(3,f[1]);scalar(4,f[2]);m.textures[1]=gpu_.floatTexture(dynamic,64,1);break;
    case 7:require(9);m.program=programId("spriteEffect_haze");m.parameters[0]=float(int(f[6]));extraRange(0,6);m.constants[38]=6.28310013f;break;
    case 21:require(7);m.program=programId(f[6]?"sprite_FilterBlur_LinearDodge":"sprite_FilterBlur");range(2,4);extra({f[0],f[1],float(bool(f[6]))});if(f[6])m.blend=2;break;
    case 24:{require(13);m.program=programId("Sprite_GhostColor");extraRange(0,6);std::copy(f.begin()+7,f.begin()+13,m.constants.begin()+38);m.constants[44]=f[6];m.constants[45]=6.28310013f;
        auto mask=assets_.frame("sprite/shader/mask.img",3);auto b=mask->cropped();m.parameters[0]=b.empty()?0:b[0]/255.f;break;}
    case 30:require(8);m.program=programId("Text_ColorGlow");range(0,4);extraRange(4,4);m.elapsed=float(c.channel);break;
    case 31:require(1);m.program=programId("spriteEffect_invert_gray");range(0,1);break;
    case 32:require(4);m.program=programId("spriteEffect_multiply_color");range(0,4);break;
    case 33:require(5);if(mode==0){m.program=programId("spriteEffect_hsbc");range(0,5);m.blend=128;}else if(mode==1){m.program=programId("spriteEffect_hsbc_filter");canvas();params({float(c.canvas->width),float(c.canvas->height)});extraRange(0,5);}else throw Error("invalid HSBC mode");break;
    case 34:require(4);m.program=programId("spriteEffect_MaskCircle");{float x=f.size()>8?f[6]:0,y=f.size()>8?f[7]:0,clip=f.size()>4?f[4]:-1;params({(x-c.cacheX)*c.cacheScale+c.cacheX+f[0]*c.cacheScale,(y-c.cacheY)*c.cacheScale+c.cacheY+f[1]*c.cacheScale,f[2]*c.cacheScale,f[3],float(clip>=0),clip});}break;
    case 35:require(8);m.program=programId("spriteEffect_maskCircleMyCharacter");params({c.cacheX*(1-c.cacheScale)+f[0]*c.cacheScale,c.cacheY*(1-c.cacheScale)+(f[1]-60)*c.cacheScale,f[2]*c.cacheScale,f[3],f[4]*c.cacheScale,f[5]});break;
    case 36:{
        std::vector<float> d;for(auto b:e->bits)d.push_back(std::bit_cast<float>(b));bool valid=d.size()==24&&replay_;bool bound[2]{};
        for(int slot=1;slot<=2;slot++){int index=slot==1?20:22;auto path=valid?replay_->path(int(d[index])):std::string{};int frame=valid?int(d[index+1]):0;
            if(!path.empty()&&frame>0){frameTexture(slot,assets_.frame(path,frame));bound[slot-1]=true;}}
        float common[4]={1,1,1,1};if(d.size()>=4)std::copy(d.begin(),d.begin()+4,common);float level=valid?d[6]:0,angle=valid?d[18]:0;
        if(bound[1]){m.program=programId("sprite_FreeFormGaugeDissolve");float transition=std::clamp((-850-int(level*-1000))/150.f,0.f,1.f);params({angle,.5f,.5f,level+d[7]*transition,d[7],d[8]*(1-transition)});for(int k=0;k<8;k++)m.constants[32+k]=d[9+k];}
        else {m.program=programId("sprite_FreeFormGauge");float edge=valid&&d[17]>0?d[17]*(1-level):0;params({angle,.5f,.5f,level,edge,valid?float(d[19]>0):0});}
        std::copy(common,common+4,m.constants.begin()+40);break;
    }
    case 38:{require(25);m.program=54;int current=int(f[8]);bool restarted=current>=int(f[7]);if(restarted)current=0;
        float delay=f[3],previous=restarted?0:f[5],accumulated=restarted?0:f[6];bool finished=!restarted&&f[0];float delta=current-previous;if(delta<0)delta+=int(f[4]+f[3]);delay-=delta;float progress=f[18];
        if(delay<=0&&!finished){float ratio=std::fmin((accumulated+delta)/f[4],1.f);if(int(f[2])==0){if(int(f[1])==0)progress=ratio;else if(int(f[1])==1)progress=1-ratio;}
            else if(int(f[2])==1){if(int(f[1])==0)progress=std::cos(ratio*1.570796f+3.141592f)+1;else if(int(f[1])==1)progress=std::cos((ratio+1)*1.570796f)+1;}progress=std::clamp(progress,0.f,1.f);}
        params({f[17]*progress,f[21],f[24]});extraRange(13,4);tex(1,std::max(0,int(f[20])),int(f[19])>=0&&int(f[19])<6?int(f[19]):0);tex(2,std::max(0,int(f[23])),int(f[22])>=0&&int(f[22])<6?int(f[22]):0);break;
    }
    case 39:{require(14);m.program=55;int current=int(f[7]);bool restarted=current>=int(f[6]);if(restarted)current=0;float previous=restarted?0:f[4],acc=restarted?0:f[5];float delta=current-previous;if(delta<0)delta+=int(f[3]);float elapsed=!restarted&&f[0]?acc:acc+delta;
        params({c.imagePresent?(f[12]-f[10])/(c.width*c.scaleX):0,c.imagePresent?(f[13]-f[11])/(c.height*c.scaleY):0,elapsed,f[3],float(int(f[1]))});tex(1,std::max(0,int(f[9])),int(f[8])>=0&&int(f[8])<6?int(f[8]):0);break;
    }
    case 40:{require(17);if(f[16]>0)require(18);m.program=65;int duration=int(f[13]),current=f[16]>0?int(f[16]):0,length=f[16]>0?int(f[17]):duration;bool restarted=current>=length;if(restarted){current=0;length=duration;}
        float random=f[15];if(restarted){randomState_=214013u*randomState_+2531011u;random=((randomState_>>16)&0x7fff)%100*.01f;}
        auto mask=assets_.frame("sprite/shader/glitchtexture.img",0);auto pixels=mask->cropped();float ratio=float(current)/length;float x=(mask->width-1)*std::fmin(random,1.f),y=(mask->height-1)*std::fmin(ratio,1.f),target=0;
        if(std::isfinite(x)&&std::isfinite(y)&&x>=0&&y>=0&&x<mask->width&&y<mask->height)target=pixels[(int(y)*mask->width+int(x))*4]/255.f;
        float tm=duration*ratio,adjusted=tm<f[11]?tm+duration:tm;float interpolated=f[10]+(adjusted-f[11])*(target-f[10])*f[14];params({target-f[10]<=0?std::fmax(target,interpolated):std::fmin(target,interpolated),tm});extraRange(0,10);m.constants[42]=f[12];m.constants[43]=6.28310013f;m.constants[44]=m.constants[45]=50;break;
    }
    case 41:{require(8);m.program=78;float x=f.size()>12?f[10]:0,y=f.size()>12?f[11]:0,angle=f.size()>8?f[8]:0;params({(x-c.cacheX)*c.cacheScale+c.cacheX+f[0]*c.cacheScale,(y-c.cacheY)*c.cacheScale+c.cacheY+f[1]*c.cacheScale,f.size()>14?float(int(f[13])):float(c.width),f.size()>14?float(int(f[14])):float(c.height),float(f.size()>8&&angle>=0),angle});extra({f[2]*float(3.141592653589793/180),f[3]*float(.5*3.141592653589793/180),f[4]*c.cacheScale,f[5]*c.cacheScale,f[6],float(bool(f[7]))});break;}
    case 42:{require(5);m.program=79;float x=f.size()>8?f[7]:0,y=f.size()>8?f[8]:0,threshold=f.size()>5?f[5]:0;params({(x-c.cacheX)*c.cacheScale+c.cacheX,(y-c.cacheY)*c.cacheScale+c.cacheY,f[2]*c.cacheScale,f[3]*c.cacheScale,float(f.size()>5&&threshold>=0),threshold});extra({f[4]});break;}
    case 44:{m.program=89;size_t n=std::min(f.size()/4,size_t(20));m.constants[32]=float(n);std::copy(f.begin(),f.begin()+n*4,m.constants.begin()+36);break;}
    case 46:require(4);m.program=101;params({f[0],f[1],0,0});break;
    case 47:{require(11);m.program=52;int duration=int(f[0]);float ratio=0.f/duration,radius=f[4]+(f[3]-f[4])*ratio;if(f[9])radius=duration<=0?f[3]:f[4];float x=f[1],y=f[2];if(f[10]>0){x=(x-c.x)/c.width;y=(y-c.y)/c.height;if(c.direction==0)x=1-x;}params({x,y,radius});extraRange(5,4);tex(1,4);break;}
    case 48:require(2);m.program=96;params({0,f[0],f[1],1});break;
    case 49:require(3);m.program=97;range(0,3);extra({20000});tex(3,0,4);break;
    case 52:require(3);m.program=programId("sprite_WaterReflection");range(0,3);tex(1,0,4);break;
    case 54:{std::array<float,7> v{};if(f.size()==7)std::copy(f.begin(),f.end(),v.begin());auto sub=c.external[1];m.textures[1]=sub;int mode=int(v[5]);if(mode==3){m.program=programId("sprite_ReverseMaskingShader");params({v[2],v[3],v[0],v[1],sub?float(sub->width):1,sub?float(sub->height):1});}else{m.program=programId(c.actorFacing==2?"sprite_MaskingShader_LinearDodge":"sprite_MaskingShader");params({v[2],v[3],v[0],v[1],float(mode)});}break;}
    case 55:require(1);m.program=programId("spriteEffect_BasicWarning");params({float(f[0]>=0),f[0]});break;
    case 56:m.program=programId("sprite_StoneStatue");m.textures[1]=c.external[1];if(c.external[1])params({.15f,float(c.stoneMode)});break;
    case 58:require(4);m.program=programId("spriteEffect_ClipSprite");range(0,4);break;
    case 64:require(10);m.program=programId("spriteEffect_ChainLine");params({f[0],f[1],f[2],f[3],f[5],f[6]});extra({f[4],f[7],f[8],f[9]});break;
    case 66:m.program=programId("Sprite_MultiTextureBlend");m.textures[1]=c.external[1];if(c.external[1]){m.parameters[0]=f.empty()?0:f[0];std::copy(c.multiColor.begin(),c.multiColor.end(),m.parameters.begin()+1);}break;
    default:throw Error("unimplemented legal shader type "+std::to_string(t));
    }
    for(int slot=1;slot<4;slot++)if(m.textures[slot]){m.constants[slot*4]=m.textures[slot]->width;m.constants[slot*4+1]=m.textures[slot]->height;m.constants[slot*4+2]=1-.5f/m.textures[slot]->width;m.constants[slot*4+3]=1-.5f/m.textures[slot]->height;}
    return m;
}
}
