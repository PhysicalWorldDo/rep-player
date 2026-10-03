#include "fonts.hpp"
#include <algorithm>
#include <cmath>
#include <windows.h>

namespace rep {
static std::filesystem::path systemTahoma(){
    wchar_t directory[MAX_PATH]{};
    if(!GetWindowsDirectoryW(directory,MAX_PATH))throw Error("Cannot find Windows font directory");
    return std::filesystem::path(directory)/L"Fonts"/L"tahoma.ttf";
}
static std::filesystem::path fontPath(const std::filesystem::path& root,int id,int weight) {
    const wchar_t* name=nullptr;
    switch(id){case 1:name=L"DNFForgedBlade-Light.ttf";break;case 2:name=L"DNFForgedBlade-Medium.ttf";break;case 3:name=L"DNFForgedBlade-Bold.ttf";break;case 4:name=L"NotoSansKR-Regular.ttf";break;case 5:name=L"NotoSansKR-Medium.ttf";break;case 6:name=L"NotoSansKR-Bold.ttf";break;case 36:name=L"DNFBitBitv2.ttf";break;}
    if(name&&std::filesystem::exists(root/name))return root/name;
    if(id==0||id==7||id==8||id==34||id==35){auto p=root/L"tahoma.ttf";return std::filesystem::exists(p)?p:systemTahoma();}
    if(!name){auto p=root/L"DNFForgedBlade-Medium.ttf";if(std::filesystem::exists(p))return p;p=root/L"gulim.ttc";if(std::filesystem::exists(p))return p;}
    // Chinese clients register a different font family. Resolve only files in
    // the selected client, retaining the original face when it is available.
    bool light=id==1,bold=id==3||id==6||id==36||(!name&&weight>=600);
    const wchar_t* preferred=light?L"NotoSansSC-Light.otf":bold?L"NotoSansSC-Bold.otf":L"NotoSansSC-Regular.otf";
    for(auto candidate:{preferred,bold?L"NotoSansCJKsc-Bold.otf":L"NotoSansCJKsc-Regular.otf",L"NotoSansSC-Regular.otf",L"NotoSansCJKsc-Regular.otf",L"gulim.ttc"}){
        auto p=root/candidate;if(std::filesystem::exists(p))return p;
    }
    return systemTahoma();
}
static void ftCheck(FT_Error e){if(e)throw Error("FreeType error "+std::to_string(e));}
Rasterizer::Rasterizer(std::filesystem::path root):root_(std::move(root)){ftCheck(FT_Init_FreeType(&library_));}
Rasterizer::~Rasterizer(){for(auto [key,face]:faces_)FT_Done_Face(face);FT_Done_FreeType(library_);}
FT_Face Rasterizer::face(int id,int height,int weight) {
    auto file=fontPath(root_,id,weight);auto path=utf8(file.wstring());auto key=std::pair{path,height};if(auto it=faces_.find(key);it!=faces_.end())return it->second;
    // FreeType's Windows pathname loader uses narrow fopen. A filesystem path
    // reads Unicode client directories correctly; bytes must outlive all faces.
    auto it=fontFiles_.find(path);if(it==fontFiles_.end())it=fontFiles_.emplace(path,readFile(file)).first;
    auto& bytes=it->second;FT_Face f;ftCheck(FT_New_Memory_Face(library_,bytes.data(),FT_Long(bytes.size()),0,&f));
    if(auto error=FT_Set_Pixel_Sizes(f,0,height)){FT_Done_Face(f);ftCheck(error);}return faces_[key]=f;
}
std::shared_ptr<Glyph> Rasterizer::glyph(int id,int height,int weight,int slant,int outline,uint32_t character) {
    auto key=std::tuple{id,height,weight,slant,outline,character};if(auto it=glyphs_.find(key);it!=glyphs_.end())return it->second;
    auto f=face(id,height,weight);int actualId=id;FT_Int32 flags=((id==0||id==26)&&height==11)?0x21000:0;FT_Render_Mode mode=flags?FT_RENDER_MODE_MONO:FT_RENDER_MODE_NORMAL;
    if(slant){flags=8;mode=FT_RENDER_MODE_NORMAL;}if(id==4||(id>=6&&id<=8))flags|=2;if(id==10)flags|=0x22;
    auto index=FT_Get_Char_Index(f,character==8202?32:character);if(!index)for(int fallback:{0,1,2,3,4,5,6,36}){auto candidate=face(fallback,height,weight);index=FT_Get_Char_Index(candidate,character==8202?32:character);if(index){f=candidate;actualId=fallback;break;}}
    ftCheck(FT_Load_Glyph(f,index,flags));auto slot=f->glyph;bool outlined=slot->format==FT_GLYPH_FORMAT_OUTLINE;
    if(outlined&&slant){FT_Matrix matrix{65536,long(slant/32.f*65536),0,65536};FT_Outline_Transform(&slot->outline,&matrix);}
    int adjusted=weight+(id==4&&mode!=FT_RENDER_MODE_MONO?200:0);float ratio=adjusted*.0025f;
    if(std::abs(ratio-1)>std::abs(ratio)*std::numeric_limits<float>::epsilon()) {
        if(outlined){if(ratio<1)ratio-=2;ftCheck(FT_Outline_Embolden(&slot->outline,long(float(f->size->metrics.y_ppem)*ratio*.64f)));}
        else {ftCheck(FT_Render_Glyph(slot,mode));ftCheck(FT_Bitmap_Embolden(library_,&slot->bitmap,long((ratio-1)*64),0));}}
    ftCheck(FT_Render_Glyph(slot,mode));if(outline>0&&slot->bitmap.buffer){ftCheck(FT_GlyphSlot_Own_Bitmap(slot));FT_Bitmap_Embolden(library_,&slot->bitmap,outline<<6,outline<<6);}
    auto result=std::make_shared<Glyph>();auto& b=slot->bitmap;result->width=b.width;result->height=b.rows;result->coverage.resize(size_t(b.width)*b.rows);
    int inkLeft=INT_MAX,inkRight=0;for(unsigned y=0;y<b.rows;y++)for(unsigned x=0;x<b.width;x++) {auto line=b.buffer+(b.pitch<0?(b.rows-1-y):y)*std::abs(b.pitch);auto v=b.pixel_mode==FT_PIXEL_MODE_MONO?((line[x/8]>>(7-x%8))&1)*255:line[x];result->coverage[y*b.width+x]=uint8_t(v);
        if(v>(b.pixel_mode==FT_PIXEL_MODE_MONO?0:12)){inkLeft=std::min(inkLeft,int(x));inkRight=std::max(inkRight,int(x)+1);}}
    if(inkLeft==INT_MAX)inkLeft=0;bool nonempty=inkLeft||inkRight,space=character==32;
    auto normalFace=face(0,height,weight);int baseline=int(int64_t(normalFace->ascender)*normalFace->size->metrics.y_scale/4194304);
    int left=0,top=baseline-slot->bitmap_top,advance=0,blank=(2*(height>>1)+1+2*height)/3;
    if(id==0){if(nonempty&&!space){left=slant?slot->bitmap_left:-inkLeft;advance=slant?slot->advance.x>>6:inkRight+height/2-inkLeft;}else advance=blank;
        if(character=='1'||character=='I'){advance+=2;left+=character=='I';}else if(character==728&&height==11)advance++;}
    else if(id==4){if(space)advance=blank-1;else if(nonempty){left=slot->bitmap_left;advance=int(std::round((slot->advance.x-44)/64.f));if(actualId!=2)top--;if(character=='+'){left++;advance++;}else if(character=='*')advance++;}else advance=blank;}
    else if(id>=6&&id<=8){left=slot->bitmap_left;if(nonempty&&!space){advance=int(std::round(slot->advance.x/64.f+.5625f));top-=2;}else advance=blank;}
    else if(id==11||id==12||id==13||id==31||id==32||id==33){left=slot->bitmap_left;if(nonempty&&!space){advance=slot->advance.x>>6;top-=4;}else advance=blank;}
    else if(id==14){if(nonempty&&!space){left=slant?slot->bitmap_left:-inkLeft;advance=slant?slot->advance.x>>6:inkRight+height/2-inkLeft+1;}else{left=slot->bitmap_left;advance=blank;}}
    else {left=slot->bitmap_left;advance=nonempty&&!space?slot->advance.x>>6:blank;if(id==26)top-=2;}
    if(character==8202)advance=1;result->left=left;result->top=top;result->advance=advance;return glyphs_[key]=result;
}
std::pair<int,int> Rasterizer::linePositions(int height){auto f=face(0,height,400);int base=int(int64_t(f->ascender)*f->size->metrics.y_scale/4194304);int u=int(float(f->underline_position)*f->size->metrics.y_ppem/f->units_per_EM);int y=base+u/2-u;return {y,y-base/2};}
Atlas::Node* Atlas::Node::allocate(int width,int height) {
    if(left){if(auto node=left->allocate(width,height))return node;return right->allocate(width,height);}
    if(occupied||width>w||height>h)return nullptr;if(width==w&&height==h){occupied=true;return this;}
    int dw=w-width,dh=h-height;if(dw<=dh){left=std::make_unique<Node>(x,y,w,height);right=std::make_unique<Node>(x,y+height,w,dh);}
    else {left=std::make_unique<Node>(x,y,width,h);right=std::make_unique<Node>(x+width,y,dw,h);}return left->allocate(width,height);
}
Atlas::Atlas(){pixels=std::make_shared<Pixels>();pixels->width=pixels->height=1024;packed_.resize(1024*1024);pixels->rgba.resize(1024*1024*4);root_=std::make_unique<Node>(0,0,1024,1024);}
bool Atlas::advance() {
    if(format_==5)format_=6;else {int w=pixels->width,h=pixels->height,x=originX_,y=originY_;
        if(x+1024>=w){if(y+1024>=h){if(w*2>4096||h*2>4096)return false;std::vector<uint16_t> grown(size_t(w*2)*(h*2));for(int row=0;row<h;row++)std::copy_n(packed_.data()+row*w,w,grown.data()+row*w*2);packed_=std::move(grown);pixels->width=w*2;pixels->height=h*2;pixels->rgba.resize(size_t(w*2)*(h*2)*4);x=w;y=0;}
            else{x=w/2;if(y+1024>=h/2)x=0;y+=1024;}}
        else x+=1024;originX_=x;originY_=y;format_=5;}
    root_=std::make_unique<Node>(originX_,originY_,1024,1024);dirty=true;return true;
}
void Atlas::upload(const Glyph& g,int x,int y,int shift,int scale) {
    for(int row=0;row<g.height*scale;row++)for(int col=0;col<g.width*scale;col++){int xx=x+col,yy=y+row;if(xx<0||yy<0||xx>=pixels->width||yy>=pixels->height)continue;
        packed_[size_t(yy)*pixels->width+xx]|=uint16_t((g.coverage[(row/scale)*g.width+col/scale]>>4)<<shift);}
    dirty=true;
}
std::shared_ptr<GlyphRegistration> Atlas::registerGlyph(std::string key,const Glyph& n,const Glyph& b,int padding,int scale) {
    if(auto it=registrations_.find(key);it!=registrations_.end())return it->second;padding=padding?padding:1;int margin=padding==1?0:padding,write=padding+margin;
    int w=(std::max(n.width,b.width)*scale+2*write)&65535,h=(std::max(n.height,b.height)*scale+2*write)&65535;Node* node;
    while(!(node=root_->allocate(w,h)))if(!advance())return registrations_[key]={};
    auto r=std::make_shared<GlyphRegistration>();int x=node->x+padding,y=node->y+padding;r->normal={x,y,x+n.width*scale+2*margin,y+n.height*scale+2*margin};r->outline={x,y,x+b.width*scale+2*margin,y+b.height*scale+2*margin};r->format=format_;r->scale=scale;
    upload(n,node->x+write,node->y+write,format_==5?8:0,scale);upload(b,node->x+write,node->y+write,format_==5?4:12,scale);return registrations_[key]=r;
}
void Atlas::refresh(){if(!dirty)return;int shifts[]={8,4,0,12};for(size_t i=0;i<packed_.size();i++)for(int c=0;c<4;c++)pixels->rgba[i*4+c]=uint8_t(((packed_[i]>>shifts[c])&15)*17);dirty=false;}
static std::pair<int,int> configuration(int id) {
    switch(id){case 0:case 22:case 34:case 35:return {11,id==22?100:400};case 7:return {13,100};case 8:return {18,100};case 9:return {30,700};case 10:return {26,700};case 11:return {28,700};case 12:return {30,400};case 13:return {20,400};case 19:return {17,700};case 23:return {26,700};case 24:return {15,100};case 25:return {28,700};case 26:return {22,100};case 27:return {24,100};default:return {15,400};}
}
std::array<int32_t,11> Fonts::parameters(const Instruction& i,const Replay& r) {
    std::array<int32_t,11> words{};if(i.opcode==48){std::memcpy(words.data(),i.native.data(),44);return words;}
    uint8_t type,style;uint32_t color,flags=0;
    if(r.version<15||r.header.minor<5){type=i.native[0];style=i.native[8];color=at<uint32_t>(i.native,4);}
    else {type=i.native[4];style=i.native[5];color=at<uint32_t>(i.native,8);flags=at<uint32_t>(i.native,16);}
    static const std::pair<int,int> mapping[]={ {1,8},{2,9},{3,10},{4,11},{5,12},{12,36},{13,28},{14,7},{15,13},{16,19},{17,27},{18,26},{19,24},{20,22},{21,25},{22,23},{23,34},{24,35}};
    int id=0;for(auto [key,value]:mapping)if(key==type)id=value;auto [height,weight]=configuration(id);
    words={height,weight,id,0,2,1,0,int32_t(color),int32_t((color&0xff000000)|0xffffff),0,int32_t((style==1?1:0)|(flags==2?1<<16:flags==1?1<<24:0))};return words;
}
std::vector<GlyphQuad> Fonts::prepare(const Instruction& i,const Replay& r,float x,float y) {
    std::vector<GlyphQuad> quads;auto text=wide(r.path(i.resource));if(text.empty())return quads;
    auto p=parameters(i,r);int id=p[2];if(id==-1)return quads;auto [cfgHeight,cfgWeight]=configuration(id);int size=std::max(1,p[0]?std::abs(p[0]):cfgHeight),weight=p[1]?p[1]:cfgWeight,padding=p[5]?p[5]:1;
    bool fixed=id==0&&(padding==0||padding==1)&&size>=14;int sourceSize=fixed?11:size,atlasScale=fixed?2:1;float glyphScale=float(size)/sourceSize;int margin=padding==1?0:padding;
    auto& owner=atlases_[id];if(!owner)owner=std::make_unique<Atlas>();auto& atlas=*owner;bool outline=(uint32_t(p[10])&255)==1,gradient=((uint32_t(p[10])>>8)&255)==1;
    struct Positioned {float advance;std::shared_ptr<Glyph> normal,border;std::shared_ptr<GlyphRegistration> registration;};std::vector<Positioned> positions;float advance=0;
    for(size_t n=0;n<text.size();n++){uint32_t code=text[n];if(code>=0xd800&&code<=0xdbff&&n+1<text.size()){code=0x10000+((code-0xd800)<<10)+(text[++n]-0xdc00);}
        auto normal=rasterizer_.glyph(id,sourceSize,weight,p[3],0,code),border=rasterizer_.glyph(id,sourceSize,weight,p[3],std::max(0,p[4]),code);
        std::string key=std::to_string(sourceSize)+":"+std::to_string(weight)+":"+std::to_string(p[3])+":"+std::to_string(p[4])+":"+std::to_string(padding)+":"+std::to_string(atlasScale==2?0:p[6])+":"+std::to_string(atlasScale)+":"+std::to_string(code);
        auto registration=atlas.registerGlyph(key,*normal,*border,padding,atlasScale);positions.push_back({advance,normal,border,registration});advance+=int(normal->advance+p[6]*sourceSize*.001f);}
    auto add=[&](std::shared_ptr<GlyphRegistration> reg,bool border,float gx,float gy,uint32_t color,bool grad){if(!reg)return;auto rect=border?reg->outline:reg->normal;int width=std::abs(rect[2]-rect[0]),height=std::abs(rect[3]-rect[1]);if(!width||!height)return;
        auto frame=std::make_shared<Frame>();frame->width=frame->fullWidth=width;frame->height=frame->fullHeight=height;frame->rect=rect;frame->texture=atlas.pixels;int channel=reg->format==5?(border?2:1):(border?4:3);
        quads.push_back({frame,gx-margin*glyphScale/atlasScale,gy-margin*glyphScale/atlasScale,glyphScale/atlasScale,color,uint32_t(p[9]),channel,grad});};
    if(outline)for(auto& g:positions){int shift=p[4]/2;add(g.registration,true,x+(g.advance+g.normal->left-shift)*glyphScale,y+(g.normal->top-shift)*glyphScale,uint32_t(p[8]),false);}
    for(auto& g:positions)add(g.registration,false,x+(g.advance+g.normal->left)*glyphScale,y+g.normal->top*glyphScale,uint32_t(p[7]),gradient);
    bool underline=((uint32_t(p[10])>>16)&255)==1,strike=((uint32_t(p[10])>>24)&255)==1;
    if(underline||strike){Glyph line;line.width=std::max(0,int(advance));line.height=1;line.coverage.assign(line.width,255);Glyph blank;
        auto registration=atlas.registerGlyph("line:"+std::to_string(line.width)+":"+std::to_string(padding)+":"+std::to_string(atlasScale),line,blank,padding,atlasScale);auto [u,s]=rasterizer_.linePositions(sourceSize);if(underline)add(registration,false,x,y+u*glyphScale,uint32_t(p[7]),false);if(strike)add(registration,false,x,y+s*glyphScale,uint32_t(p[7]),false);}
    atlas.refresh();return quads;
}
}
