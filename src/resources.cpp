#include "resources.hpp"
#include <algorithm>
#include <fstream>

namespace rep {
std::shared_ptr<Pixels> nativeImageTexture(std::shared_ptr<Pixels> pixels){
    int w=pixels->width,h=pixels->height;pixels->logicalWidth=w;pixels->logicalHeight=h;
    if(w<=512&&h<=512){auto power=[](int n){int p=16;while(p<n)p*=2;return p;};int pw=power(w),ph=power(h);
        if(pw!=w||ph!=h){Bytes padded(size_t(pw)*ph*4);for(int y=0;y<h;y++)std::copy_n(pixels->rgba.data()+size_t(y)*w*4,w*4,padded.data()+size_t(y)*pw*4);pixels->width=pw;pixels->height=ph;pixels->rgba=std::move(padded);}}
    return pixels;
}
static std::shared_ptr<Pixels> rawPixels(std::span<const uint8_t> raw,int fmt,int w,int h,const Bytes* palette=nullptr) {
    if(w<=0||h<=0)throw Error("invalid IMG dimensions");
    auto out=std::make_shared<Pixels>();out->width=w;out->height=h;out->rgba.resize(size_t(w)*h*4);
    auto& b=out->rgba;
    if(palette) {
        if(raw.size()!=size_t(w)*h)throw Error("invalid palette pixel length");
        for(size_t p=0;p<raw.size();p++){size_t ix=size_t(raw[p])*4;if(ix+4>palette->size())throw Error("palette index out of range");std::memcpy(b.data()+p*4,palette->data()+ix,4);}
    } else if(fmt==16) {
        if(raw.size()!=b.size())throw Error("invalid BGRA pixel length");
        for(size_t p=0;p<b.size();p+=4){b[p]=raw[p+2];b[p+1]=raw[p+1];b[p+2]=raw[p];b[p+3]=raw[p+3];}
    } else if(fmt==14||fmt==15) {
        if(raw.size()!=size_t(w)*h*2)throw Error("invalid 16-bit pixel length");
        for(size_t p=0;p<size_t(w)*h;p++){auto x=at<uint16_t>(raw,p*2);
            if(fmt==14){for(int c=0;c<3;c++){int n=(x>>(10-c*5))&31;b[p*4+c]=uint8_t((n<<3)|(n>>2));}b[p*4+3]=(x>>15)*255;}
            else {int shifts[]={8,4,0,12};for(int c=0;c<4;c++)b[p*4+c]=uint8_t(((x>>shifts[c])&15)*17);}
        }
    } else if(fmt>=18&&fmt<=20) {
        if(raw.size()<128||std::memcmp(raw.data(),"DDS ",4))throw Error("missing DDS header");
        if(at<uint32_t>(raw,12)!=uint32_t(h)||at<uint32_t>(raw,16)!=uint32_t(w))throw Error("DDS dimensions disagree with IMG");
        uint32_t fourcc=at<uint32_t>(raw,84);int mode=fourcc==0x31545844?1:fourcc==0x33545844?2:fourcc==0x35545844?3:0;
        if(!mode)throw Error("unsupported DDS compression");size_t offset=128;
        for(int by=0;by<h;by+=4)for(int bx=0;bx<w;bx+=4) {
            size_t stride=mode==1?8:16;if(offset+stride>raw.size())throw Error("truncated DDS block");auto block=raw.subspan(offset,stride);offset+=stride;
            size_t co=mode==1?0:8;uint16_t a=at<uint16_t>(block,co),d=at<uint16_t>(block,co+2);uint32_t indices=at<uint32_t>(block,co+4);
            std::array<std::array<uint8_t,4>,4> colors{};
            for(int k=0;k<2;k++){uint16_t n=k?d:a;int r=(n>>11)&31,g=(n>>5)&63,blue=n&31;colors[k]={uint8_t((r<<3)|(r>>2)),uint8_t((g<<2)|(g>>4)),uint8_t((blue<<3)|(blue>>2)),255};}
            if(a>d||mode!=1){for(int c=0;c<3;c++){colors[2][c]=(2*colors[0][c]+colors[1][c])/3;colors[3][c]=(colors[0][c]+2*colors[1][c])/3;}colors[2][3]=colors[3][3]=255;}
            else {for(int c=0;c<3;c++)colors[2][c]=(colors[0][c]+colors[1][c])/2;colors[2][3]=255;colors[3]={0,0,0,0};}
            std::array<uint8_t,8> alpha{};uint64_t abits=0;
            if(mode==3){alpha[0]=block[0];alpha[1]=block[1];if(alpha[0]>alpha[1])for(int k=2;k<8;k++)alpha[k]=((8-k)*alpha[0]+(k-1)*alpha[1])/7;
                else {for(int k=2;k<6;k++)alpha[k]=((6-k)*alpha[0]+(k-1)*alpha[1])/5;alpha[6]=0;alpha[7]=255;}for(int k=0;k<6;k++)abits|=uint64_t(block[k+2])<<(k*8);}
            for(int p=0;p<16;p++){int x=bx+p%4,y=by+p/4;if(x>=w||y>=h)continue;auto color=colors[(indices>>(2*p))&3];
                if(mode==2)color[3]=uint8_t(((block[p/2]>>((p%2)*4))&15)*17);else if(mode==3)color[3]=alpha[(abits>>(p*3))&7];
                std::memcpy(b.data()+(size_t(y)*w+x)*4,color.data(),4);}
        }
    } else throw Error("unsupported IMG color format "+std::to_string(fmt));
    return out;
}
Bytes Frame::cropped() const {
    Bytes out(size_t(width)*height*4);if(empty)return out;
    for(int y0=0;y0<height;y0++)for(int x0=0;x0<width;x0++) {
        int x=rotated?rect[2]-1-y0:rect[0]+x0,y=rotated?rect[1]+x0:rect[1]+y0;
        x=std::clamp(x,0,texture->width-1);y=std::clamp(y,0,texture->height-1);
        std::memcpy(out.data()+(size_t(y0)*width+x0)*4,texture->rgba.data()+(size_t(y)*texture->width+x)*4,4);
    }
    return out;
}
Img::Img(Bytes data):data_(std::move(data)) {
    Reader r(data_);auto magic=r.take(16);uint32_t indexBytes=0,n=0;
    if(!std::memcmp(magic.data(),"Neople Img File\0",16)){indexBytes=r.get<uint32_t>();r.take(4);version=r.get<uint32_t>();n=r.get<uint32_t>();}
    else if(data_.size()>=32&&!std::memcmp(data_.data(),"Neople Image File\0",18)){r.pos=20;r.take(4);version=r.get<uint32_t>();n=r.get<uint32_t>();}
    else throw Error("invalid IMG magic");
    if(version!=1&&version!=2&&version!=4&&version!=5&&version!=6)throw Error("unsupported IMG version");
    uint32_t atlasCount=0;if(version==5){atlasCount=r.get<uint32_t>();r.take(4);}
    uint32_t palettes=(version==4||version==5)?1:version==6?r.get<uint32_t>():0;
    while(palettes--){auto count=r.get<uint32_t>();auto b=r.take(size_t(count)*4);palettes_.emplace_back(b.begin(),b.end());}
    while(atlasCount--){std::vector<int32_t> a;for(int k=0;k<7;k++)a.push_back(r.get<int32_t>());atlases_.push_back(std::move(a));}
    auto start=r.pos;
    while(n--){std::vector<int32_t> a;a.push_back(r.get<int32_t>());int len=a[0]==17?2:9;
        for(int k=1;k<len;k++)a.push_back(r.get<int32_t>());
        if(a[0]!=17){if(a[1]==7)for(int k=0;k<7;k++)a.push_back(r.get<int32_t>());else if(a[1]!=5&&a[1]!=6)throw Error("unsupported IMG compression");}
        if(version==1){offsets_.push_back(r.pos);if(a[0]!=17){if(a[1]==5)a[4]=a[2]*a[3]*(a[0]==16?4:2);if(a[4]<0)throw Error("negative IMG pixel length");r.take(a[4]);}}
        records_.push_back(std::move(a));}
    if(version!=1&&indexBytes&&r.pos-start!=indexBytes)throw Error("IMG frame table length mismatch");
    for(auto& a:atlases_){atlasOffsets_.push_back(r.pos);if(a[3]<0)throw Error("negative atlas length");r.take(a[3]);}
    if(version!=1)for(auto& a:records_){offsets_.push_back(r.pos);if(a[0]!=17&&a[1]!=7){if(a[1]==5)a[4]=a[2]*a[3]*(a[0]==16?4:2);if(a[4]<0)throw Error("negative IMG pixel length");r.take(a[4]);}}
}
std::shared_ptr<Pixels> Img::atlas(int index) {
    if(auto it=atlasTextures_.find(index);it!=atlasTextures_.end())return it->second;
    if(index<0||size_t(index)>=atlases_.size())throw Error("atlas index out of range");
    const auto& a=atlases_[index];auto raw=inflateAll(std::span(data_).subspan(atlasOffsets_[index],a[3]));
    if(raw.size()!=size_t(a[4]))throw Error("atlas raw length mismatch");return atlasTextures_[index]=nativeImageTexture(rawPixels(raw,a[1],a[5],a[6]));
}
std::shared_ptr<Frame> Img::frameInner(int index,int palette,std::unordered_set<int>& visiting) {
    if(index<0||size_t(index)>=records_.size())throw std::out_of_range("IMG frame out of range");
    uint64_t key=(uint64_t(uint32_t(index))<<32)|uint32_t(palette);if(auto it=frames_.find(key);it!=frames_.end())return it->second;
    if(!visiting.insert(index).second)throw Error("linked IMG frame cycle");
    const auto& a=records_[index];if(a[0]==17)return frames_[key]=frameInner(a[1],palette,visiting);
    auto f=std::make_shared<Frame>();f->x=a[5];f->y=a[6];f->width=a[2];f->height=a[3];f->fullWidth=a[7];f->fullHeight=a[8];
    if(f->width<=0||f->height<=0)throw Error("invalid frame dimensions");
    f->empty=f->width<2&&f->height<2&&f->x==f->fullWidth&&f->y==0;
    if(f->empty){f->texture=std::make_shared<Pixels>();f->texture->width=f->width;f->texture->height=f->height;f->texture->rgba.resize(4);f->rect={0,0,f->width,f->height};}
    else if(a[1]==7){f->atlas=true;f->texture=atlas(a[10]);f->rect={a[11],a[12],a[13],a[14]};f->rotated=a[15]!=0;
        if(a[15]!=0&&a[15]!=1)throw Error("invalid atlas rotation");
        if(f->rect[0]<0||f->rect[1]<0||f->rect[2]>f->texture->width||f->rect[3]>f->texture->height)throw Error("atlas crop out of range");
        int w=f->rect[2]-f->rect[0],h=f->rect[3]-f->rect[1];if((f->rotated?h:w)!=f->width||(f->rotated?w:h)!=f->height)throw Error("atlas crop dimensions mismatch");}
    else {auto s=std::span(data_).subspan(offsets_[index],a[4]);Bytes decoded;if(a[1]==6){decoded=inflateAll(s);s=decoded;}
        const Bytes* p=nullptr;
        if(a[1]==6&&a[0]==14&&std::any_of(palettes_.begin(),palettes_.end(),[](const auto& b){return !b.empty();})) {
            if(palette<0||size_t(palette)>=palettes_.size())throw Error("IMG palette index out of range");p=&palettes_[palette];if(p->empty())throw Error("empty IMG palette");}
        f->texture=nativeImageTexture(rawPixels(s,a[0],f->width,f->height,p));f->rect={0,0,f->width,f->height};}
    return frames_[key]=f;
}
std::shared_ptr<Frame> Img::frame(int index,int palette){std::unordered_set<int> visiting;return frameInner(index,palette,visiting);}
static std::string normalize(std::string name) {name=canonical(name);while(!name.empty()&&name[0]=='/')name.erase(0,1);if(name.rfind("sprite/",0)!=0)name="sprite/"+name;return name;}
Assets::Assets(std::filesystem::path root):root_(std::move(root)){indexNative();}
void Assets::indexNative() {
    auto path=root_/L"NpkIndex.etc";if(!std::filesystem::exists(path))return;
    auto data=readFile(path);Reader r(data);auto rawLength=r.get<uint32_t>(),zipped=r.get<uint32_t>();auto s=r.take(zipped);r.end();Bytes encoded(s.begin(),s.end());
    for(size_t k=0;k<encoded.size();k++)encoded[k]=uint8_t(uint8_t((encoded[k]^0xaa)-k*7)^0xaa);
    auto raw=inflateAll(encoded);if(raw.size()!=rawLength)throw Error("native NPK index length mismatch");Reader d(raw);
    while(d.remaining()){auto count=d.get<uint32_t>();auto n=d.get<uint32_t>();auto b=d.take(n);std::string name(reinterpret_cast<const char*>(b.data()),n);name=canonical(name);name=name.substr(name.find_last_of('/')+1);
        while(count--){n=d.get<uint32_t>();b=d.take(n);std::string logical(reinterpret_cast<const char*>(b.data()),n);auto offset=d.get<uint32_t>(),length=d.get<uint32_t>();entries_.try_emplace(normalize(logical),Entry{name,offset,length});}}
}
void Assets::indexPackage(const std::string& name) {
    if(packages_.contains(name))return;auto p=root_/wide(name);if(!std::filesystem::exists(p))return;
    std::ifstream f(p,std::ios::binary);std::array<uint8_t,20> h{};f.read(reinterpret_cast<char*>(h.data()),20);
    if(!f||std::memcmp(h.data(),"NeoplePack_Bill\0",16))throw Error("invalid NPK magic");
    auto n=at<uint32_t>(h,16);Bytes table(size_t(n)*264);f.read(reinterpret_cast<char*>(table.data()),table.size());if(!f)throw Error("truncated NPK table");
    std::string seed="puchikon@neople dungeon and fighter ";while(seed.size()<255)seed+="DNF";seed.resize(255);seed.push_back(0);
    for(uint32_t k=0;k<n;k++){auto b=std::span(table).subspan(size_t(k)*264,264);std::string logical;for(int j=0;j<256;j++){auto c=char(b[8+j]^uint8_t(seed[j]));if(!c)break;logical.push_back(c);}entries_.try_emplace(normalize(logical),Entry{name,at<uint32_t>(b,0),at<uint32_t>(b,4)});}
    packages_.insert(name);
}
Assets::Entry* Assets::resolve(std::string name) {
    name=normalize(name);if(auto it=entries_.find(name);it!=entries_.end())return &it->second;
    auto parent=name.substr(0,name.find_last_of('/'));
    while(parent.find('/')!=std::string::npos){auto package=parent;std::replace(package.begin(),package.end(),'/','_');indexPackage(package+".NPK");if(auto it=entries_.find(name);it!=entries_.end())return &it->second;parent=parent.substr(0,parent.find_last_of('/'));}
    return nullptr;
}
std::shared_ptr<Img> Assets::preload(std::string logical) {
    auto name=normalize(logical);if(auto it=images_.find(name);it!=images_.end())return it->second;
    auto e=resolve(name);if(!e)return {};
    std::ifstream f(root_/wide(e->package),std::ios::binary);if(!f)throw Error("NPK file absent: "+e->package);
    Bytes b(e->length);f.seekg(e->offset);f.read(reinterpret_cast<char*>(b.data()),b.size());if(!f)throw Error("truncated IMG entry");
    auto img=std::make_shared<Img>(std::move(b));images_[name]=img;return img;
}
std::shared_ptr<Frame> Assets::frame(std::string logical,int index,int palette,bool fallback) {
    if(logical.empty())return {};notifyFrame(normalize(logical),index);auto img=preload(logical);
    if(img&&index>=0&&size_t(index)<img->count()){decodedFrames++;auto frame=img->frame(index,palette);frame->actualPath=normalize(logical);frame->actualFrame=index;return frame;}
    if(!fallback)throw Error("missing resource/frame: "+logical);
    fallbacks++;auto base=preload("interface/base.img");if(!base)throw Error("native missing-resource fallback absent");auto frame=base->frame(91);frame->actualPath=normalize("interface/base.img");frame->actualFrame=91;notifyFrame(frame->actualPath,91);return frame;
}
}
