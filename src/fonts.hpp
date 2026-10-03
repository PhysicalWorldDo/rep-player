#pragma once
#include "resources.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_BITMAP_H
#include <map>
#include <tuple>

namespace rep {
struct Glyph {Bytes coverage;int width=0,height=0,left=0,top=0,advance=0;};
class Rasterizer {
    std::filesystem::path root_;
    FT_Library library_=nullptr;
    std::map<std::string,Bytes> fontFiles_;
    std::map<std::pair<std::string,int>,FT_Face> faces_;
    std::map<std::tuple<int,int,int,int,int,uint32_t>,std::shared_ptr<Glyph>> glyphs_;
    FT_Face face(int id,int height,int weight);
public:
    explicit Rasterizer(std::filesystem::path root={});~Rasterizer();
    std::shared_ptr<Glyph> glyph(int id,int height,int weight,int slant,int outline,uint32_t codepoint);
    std::pair<int,int> linePositions(int height);
};
struct GlyphRegistration {std::array<int,4> normal{},outline{};int format=5,scale=1;};
class Atlas {
    struct Node {
        int x,y,w,h;bool occupied=false;std::unique_ptr<Node> left,right;
        Node(int x,int y,int w,int h):x(x),y(y),w(w),h(h){}
        Node* allocate(int width,int height);
    };
    std::unique_ptr<Node> root_;
    int originX_=0,originY_=0,format_=5;
    std::vector<uint16_t> packed_;
    std::map<std::string,std::shared_ptr<GlyphRegistration>> registrations_;
    bool advance();
    void upload(const Glyph& glyph,int x,int y,int shift,int scale);
public:
    std::shared_ptr<Pixels> pixels;
    bool dirty=true;
    Atlas();
    std::shared_ptr<GlyphRegistration> registerGlyph(std::string key,const Glyph& normal,const Glyph& outline,int padding,int scale);
    void refresh();
};
struct GlyphQuad {
    std::shared_ptr<Frame> frame;
    float x=0,y=0,scale=1;
    uint32_t color=0xffffffff,lower=0;
    int channel=1;
    bool gradient=false;
};
class Fonts {
    Rasterizer rasterizer_;
    std::map<int,std::unique_ptr<Atlas>> atlases_;
public:
    explicit Fonts(std::filesystem::path root={}):rasterizer_(std::move(root)){}
    std::vector<GlyphQuad> prepare(const Instruction& instruction,const Replay& replay,float x,float y);
    static std::array<int32_t,11> parameters(const Instruction& i,const Replay& replay);
};
}
