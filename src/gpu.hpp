#pragma once
#include "resources.hpp"
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <unordered_map>

namespace rep {
template<class T> class Com {
    T* p_=nullptr;
public:
    Com()=default;~Com(){reset();}Com(const Com&)=delete;Com& operator=(const Com&)=delete;
    Com(Com&& x) noexcept:p_(x.p_){x.p_=nullptr;}Com& operator=(Com&& x) noexcept{reset();p_=x.p_;x.p_=nullptr;return *this;}
    T* get()const{return p_;}T* operator->()const{return p_;}T** out(){reset();return &p_;}
    void reset(){if(p_)p_->Release();p_=nullptr;}
    explicit operator bool()const{return p_!=nullptr;}
};
void check(HRESULT hr,const char* operation);
struct Vertex {float position[3]{},color[4]{1,1,1,1},texcoord[8][2]{};};
struct GpuTexture {Com<ID3D11Texture2D> texture;Com<ID3D11ShaderResourceView> view;std::shared_ptr<Pixels> source;int width=0,height=0,logicalWidth=0,logicalHeight=0;};
struct Target {GpuTexture image;Com<ID3D11RenderTargetView> view;Com<ID3D11Texture2D> depth;Com<ID3D11DepthStencilView> depthView;};
struct ImageInput {std::string path;int frame=0;};
struct Material {
    int program=4;
    std::array<float,6> parameters{};
    std::array<float,128> constants{};
    std::array<std::shared_ptr<GpuTexture>,4> textures{};
    float elapsed=0,row=0;
    unsigned blend=0;
    std::vector<ImageInput> inputs;
};
class Gpu {
    struct Program {
        Com<ID3D11VertexShader> vertex;
        Com<ID3D11PixelShader> pixel;
        Com<ID3D11GeometryShader> geometry;
        Com<ID3D11InputLayout> layout;
        std::array<Com<ID3D11SamplerState>,16> samplers;
    };
    std::filesystem::path shaderPath_;
    Com<ID3D11Device> device_;
    Com<ID3D11DeviceContext> context_;
    Com<IDXGISwapChain> swapchain_;
    Com<ID3D11RenderTargetView> backbuffer_;
    Com<ID3D11Buffer> vertices_,matrices_,globals_;
    Com<ID3D11RasterizerState> rasterizer_;
    std::unordered_map<int,std::unique_ptr<Program>> programs_;
    std::unordered_map<const Pixels*,std::shared_ptr<GpuTexture>> textures_;
    std::unordered_map<unsigned,Com<ID3D11BlendState>> blends_;
    std::unordered_map<unsigned,Com<ID3D11DepthStencilState>> stencils_;
    Program& program(int id);
    int viewportWidth_=0,viewportHeight_=0;
    ID3D11RenderTargetView* activeTarget_=nullptr;
    std::vector<Vertex> pending_;
    Material pendingMaterial_;
    std::array<int,4> pendingClip_{};
    unsigned pendingStencil_=0,pendingReference_=0;
    bool pendingColor_=true;
    Target straightTarget_;
    void submit(std::span<const Vertex> vertices,Material& material,std::array<int,4> clip,unsigned stencil,unsigned reference,bool writeColor);
public:
    std::string adapter;
    bool hardware=true;
    uint64_t draws=0,uploads=0;
    explicit Gpu(std::filesystem::path shaders,HWND window=nullptr);
    ID3D11Device* device()const{return device_.get();}
    ID3D11DeviceContext* context()const{return context_.get();}
    size_t createAllPrograms();
    std::shared_ptr<GpuTexture> texture(std::shared_ptr<Pixels> pixels,bool update=false);
    std::shared_ptr<GpuTexture> floatTexture(std::span<const float> values,int width,int height);
    Target target(int width,int height,bool depth=true);
    void begin(Target& target,const float* clear=nullptr);
    void draw(std::span<const Vertex> vertices,Material& material,std::array<int,4> clip,unsigned stencil=0,unsigned reference=0,bool writeColor=true);
    void capture(Target& source,Target& destination);
    std::shared_ptr<GpuTexture> view(Target& target);
    void clearStencil(Target& target);
    void flush();
    void forgetTexture(const Pixels* pixels);
    void resetTextures(){flush();textures_.clear();}
    Bytes readback(Target& target);
    Bytes screenshot(Target& target,bool straightAlpha=false);
    void present(Target& target,int width,int height);
};
}
