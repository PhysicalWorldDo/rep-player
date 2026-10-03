#include "gpu.hpp"
#include "shader_registry.hpp"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <algorithm>
#include <sstream>

namespace rep {
void check(HRESULT hr,const char* op){if(FAILED(hr)){std::ostringstream s;s<<op<<" HRESULT 0x"<<std::hex<<uint32_t(hr);throw Error(s.str());}}
Gpu::Gpu(std::filesystem::path shaders,HWND window):shaderPath_(std::move(shaders)) {
    Com<IDXGIFactory1> factory;check(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(factory.out())),"CreateDXGIFactory1");
    Com<IDXGIAdapter1> chosen;DXGI_ADAPTER_DESC1 desc{};
    for(UINT i=0;;i++){Com<IDXGIAdapter1> a;if(factory->EnumAdapters1(i,a.out())==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 d{};a->GetDesc1(&d);
        if(!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&(!chosen||d.DedicatedVideoMemory>desc.DedicatedVideoMemory)){chosen=std::move(a);desc=d;}}
    if(!chosen)throw Error("hardware DXGI adapter not available");adapter=utf8(desc.Description);
    D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0},level;
    check(D3D11CreateDevice(chosen.get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,3,D3D11_SDK_VERSION,device_.out(),&level,context_.out()),"D3D11CreateDevice hardware");
    if(window){DXGI_SWAP_CHAIN_DESC d{};d.BufferCount=2;d.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;d.OutputWindow=window;d.SampleDesc.Count=1;d.Windowed=TRUE;d.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        check(factory->CreateSwapChain(device_.get(),&d,swapchain_.out()),"CreateSwapChain");factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER);}
    D3D11_BUFFER_DESC b{};b.ByteWidth=UINT(sizeof(Vertex)*65536);b.Usage=D3D11_USAGE_DYNAMIC;b.BindFlags=D3D11_BIND_VERTEX_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    check(device_->CreateBuffer(&b,nullptr,vertices_.out()),"CreateVertexBuffer");b.ByteWidth=128;b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.CPUAccessFlags=0;
    check(device_->CreateBuffer(&b,nullptr,matrices_.out()),"CreateMatrixBuffer");b.ByteWidth=512;check(device_->CreateBuffer(&b,nullptr,globals_.out()),"CreateGlobalsBuffer");
    D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;rs.ScissorEnable=TRUE;
    check(device_->CreateRasterizerState(&rs,rasterizer_.out()),"CreateRasterizerState");
}
Gpu::Program& Gpu::program(int id) {
    if(auto it=programs_.find(id);it!=programs_.end())return *it->second;
    const ShaderRecord* record=nullptr;for(const auto& r:shaderRecords)if(r.id==id){record=&r;break;}
    if(id==-1||id==-2)for(const auto& r:shaderRecords)if(r.id==4){record=&r;break;}
    if(!record)throw Error("original shader program absent: "+std::to_string(id));
    // The registered MaskBlur VS has a generic POSITION output. Its complete
    // instruction body matches the client's common VS; use that original VS
    // with its SV_POSITION declaration so this path rasterizes under D3D11.
    auto vs=readFile(shaderPath_/wide(id==83?"8d1b89311bfc5917b728c08c.vs.dxbc":record->vs)),ps=readFile(shaderPath_/wide(record->ps));auto p=std::make_unique<Program>();
    check(device_->CreateVertexShader(vs.data(),vs.size(),nullptr,p->vertex.out()),"CreateOriginalVertexShader");
    if(id==-1){
        const char* source=R"hlsl(
cbuffer State:register(b1){float4 globals[32];}
Texture2D drawImage:register(t0);Texture2D captureImage:register(t1);Texture2D rawImage:register(t2);
SamplerState sampleLinear:register(s0);
float4 main(float4 position:SV_POSITION):SV_TARGET {
    float2 uv=position.xy/globals[0].xy;float4 color=drawImage.Sample(sampleLinear,uv);
    int mode=(int)globals[8].x;
    if(mode){float4 mask=captureImage.Sample(sampleLinear,uv);
        if(mode==1){color.rgb=mask.rgb*globals[9].rgb;color.a=rawImage.Sample(sampleLinear,uv).a*mask.a;}
        else if(mode==2){float alpha=color.a;color=mask;color.a*=alpha>0&&globals[9].a>0;}
        else color.a*=mask.a;}
    if(color.a<=0)discard;return color;
})hlsl";
        Com<ID3DBlob> blob,errors;check(D3DCompile(source,std::strlen(source),"rep_gpu_capture",nullptr,nullptr,"main","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,blob.out(),errors.out()),"CompileCaptureComposition");
        check(device_->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,p->pixel.out()),"CreateCaptureComposition");
    }else if(id==-2){
        const char* source=R"hlsl(
Texture2D image:register(t0);
float4 main(float4 position:SV_POSITION):SV_TARGET {
    float4 color=image.Load(int3(int2(position.xy),0));
    // Additive/native blend modes can store RGB energy above coverage alpha.
    // Raise coverage only as needed to represent that energy in straight UNORM RGBA.
    color.a=max(color.a,max(color.r,max(color.g,color.b)));
    if(color.a<=0)return 0;
    return float4(saturate(color.rgb/color.a),color.a);
})hlsl";
        Com<ID3DBlob> blob,errors;check(D3DCompile(source,std::strlen(source),"rep_straight_alpha",nullptr,nullptr,"main","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,blob.out(),errors.out()),"CompileStraightAlphaResolve");
        check(device_->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,p->pixel.out()),"CreateStraightAlphaResolve");
    }else check(device_->CreatePixelShader(ps.data(),ps.size(),nullptr,p->pixel.out()),"CreateOriginalPixelShader");
    Com<ID3D11ShaderReflection> reflection;check(D3DReflect(vs.data(),vs.size(),IID_ID3D11ShaderReflection,reinterpret_cast<void**>(reflection.out())),"ReflectOriginalVertexShader");
    D3D11_SHADER_DESC sd{};reflection->GetDesc(&sd);std::vector<D3D11_INPUT_ELEMENT_DESC> layout;
    for(UINT i=0;i<sd.InputParameters;i++) {D3D11_SIGNATURE_PARAMETER_DESC input{};reflection->GetInputParameterDesc(i,&input);
        UINT offset;if(!_stricmp(input.SemanticName,"POSITION"))offset=0;else if(!_stricmp(input.SemanticName,"COLOR"))offset=12;
        else if(!_stricmp(input.SemanticName,"TEXCOORD")&&input.SemanticIndex<8)offset=28+input.SemanticIndex*8;
        else continue;
        int n=input.Mask==15?4:input.Mask==7?3:input.Mask==3?2:1;
        DXGI_FORMAT formats[]={DXGI_FORMAT_UNKNOWN,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R32G32_FLOAT,DXGI_FORMAT_R32G32B32_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT};
        layout.push_back({input.SemanticName,input.SemanticIndex,formats[n],0,offset,D3D11_INPUT_PER_VERTEX_DATA,0});}
    if(!layout.empty())check(device_->CreateInputLayout(layout.data(),UINT(layout.size()),vs.data(),vs.size(),p->layout.out()),"CreateOriginalInputLayout");
    if(id==83){const char* source=R"hlsl(
cbuffer State:register(b1){float4 globals[32];}
struct Input {float4 pos:SV_POSITION;float4 color:COLOR0;float2 uv:TEXCOORD0;float2 elapsed:TEXCOORD1;float2 zoom:TEXCOORD2;float2 p0:TEXCOORD3;float2 p1:TEXCOORD4;float2 p2:TEXCOORD5;float2 offset:TEXCOORD6;float2 scale:TEXCOORD7;};
// Preserve the original PS register order: POSITION0, COLOR0, packed TEX0..7.
// D3D11 stage signatures validate semantics but do not shuffle registers.
struct Output {float4 legacy:POSITION0;float4 color:COLOR0;float2 uv:TEXCOORD0;float2 elapsed:TEXCOORD1;float2 zoom:TEXCOORD2;float2 p0:TEXCOORD3;float2 p1:TEXCOORD4;float2 p2:TEXCOORD5;float2 offset:TEXCOORD6;float2 scale:TEXCOORD7;float4 pos:SV_POSITION;};
[maxvertexcount(3)]void main(triangle Input input[3],inout TriangleStream<Output> stream){for(int n=0;n<3;n++){Output o;o.pos=input[n].pos;o.legacy=float4((o.pos.xy/o.pos.w*float2(.5,-.5)+.5)*globals[1].xy,o.pos.zw);o.color=input[n].color;o.uv=input[n].uv;o.elapsed=input[n].elapsed;o.zoom=input[n].zoom;o.p0=input[n].p0;o.p1=input[n].p1;o.p2=input[n].p2;o.offset=input[n].offset;o.scale=input[n].scale;stream.Append(o);}stream.RestartStrip();})hlsl";
        Com<ID3DBlob> blob,errors;check(D3DCompile(source,std::strlen(source),"MaskBlur_Position_Bridge",nullptr,nullptr,"main","gs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,blob.out(),errors.out()),"CompileLegacyPositionBridge");
        check(device_->CreateGeometryShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,p->geometry.out()),"CreateLegacyPositionBridge");
    }
    for(int i=0;i<16;i++){D3D11_SAMPLER_DESC s{};s.Filter=(record->pointMask&(1u<<i))?D3D11_FILTER_MIN_MAG_MIP_POINT:D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        auto address=(record->mirrorMask&(1u<<i))?D3D11_TEXTURE_ADDRESS_MIRROR:(record->borderMask&(1u<<i))?D3D11_TEXTURE_ADDRESS_BORDER:D3D11_TEXTURE_ADDRESS_CLAMP;
        s.AddressU=s.AddressV=s.AddressW=address;s.MaxLOD=D3D11_FLOAT32_MAX;s.ComparisonFunc=D3D11_COMPARISON_ALWAYS;check(device_->CreateSamplerState(&s,p->samplers[i].out()),"CreateNativeSamplerState");}
    auto raw=p.get();programs_[id]=std::move(p);return *raw;
}
size_t Gpu::createAllPrograms(){for(const auto& r:shaderRecords)program(r.id);return programs_.size();}
std::shared_ptr<GpuTexture> Gpu::texture(std::shared_ptr<Pixels> pixels,bool update) {
    if(!pixels)return {};if(auto it=textures_.find(pixels.get());it!=textures_.end()){
        if(it->second->width==pixels->width&&it->second->height==pixels->height){if(update){flush();context_->UpdateSubresource(it->second->texture.get(),0,nullptr,pixels->rgba.data(),pixels->width*4,0);}return it->second;}
        textures_.erase(it);
    }
    auto t=std::make_shared<GpuTexture>();t->source=pixels;t->width=pixels->width;t->height=pixels->height;t->logicalWidth=pixels->logicalWidth?pixels->logicalWidth:pixels->width;t->logicalHeight=pixels->logicalHeight?pixels->logicalHeight:pixels->height;
    D3D11_TEXTURE2D_DESC d{};d.Width=t->width;d.Height=t->height;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA b{pixels->rgba.data(),UINT(t->width*4),0};check(device_->CreateTexture2D(&d,&b,t->texture.out()),"UploadDecodedIMGTexture");
    check(device_->CreateShaderResourceView(t->texture.get(),nullptr,t->view.out()),"CreateIMGShaderResourceView");textures_[pixels.get()]=t;uploads++;return t;
}
std::shared_ptr<GpuTexture> Gpu::floatTexture(std::span<const float> v,int w,int h) {
    auto t=std::make_shared<GpuTexture>();t->width=w;t->height=h;D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA b{v.data(),UINT(w*16),0};check(device_->CreateTexture2D(&d,&b,t->texture.out()),"UploadNativeParameterTexture");check(device_->CreateShaderResourceView(t->texture.get(),nullptr,t->view.out()),"CreateParameterSRV");return t;
}
Target Gpu::target(int w,int h,bool depth) {
    Target t;t.image.width=w;t.image.height=h;D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    check(device_->CreateTexture2D(&d,nullptr,t.image.texture.out()),"CreateReplayRenderTarget");check(device_->CreateShaderResourceView(t.image.texture.get(),nullptr,t.image.view.out()),"CreateRenderTargetSRV");
    check(device_->CreateRenderTargetView(t.image.texture.get(),nullptr,t.view.out()),"CreateReplayRTV");
    if(depth){d.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;check(device_->CreateTexture2D(&d,nullptr,t.depth.out()),"CreateReplayStencilTexture");check(device_->CreateDepthStencilView(t.depth.get(),nullptr,t.depthView.out()),"CreateReplayDSV");}return t;
}
void Gpu::begin(Target& t,const float* clear) {
    if(!clear&&activeTarget_==t.view.get())return;
    flush();activeTarget_=t.view.get();
    ID3D11ShaderResourceView* nulls[16]{};context_->PSSetShaderResources(0,16,nulls);
    auto view=t.view.get();context_->OMSetRenderTargets(1,&view,t.depthView.get());viewportWidth_=t.image.width;viewportHeight_=t.image.height;
    D3D11_VIEWPORT vp{0,0,float(viewportWidth_),float(viewportHeight_),0,1};context_->RSSetViewports(1,&vp);context_->RSSetState(rasterizer_.get());
    if(clear){context_->ClearRenderTargetView(view,clear);if(t.depthView)context_->ClearDepthStencilView(t.depthView.get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);}
    std::array<float,32> m{};m[0]=m[5]=m[10]=m[15]=1;m[16]=2.f/viewportWidth_;m[19]=-1;m[21]=-2.f/viewportHeight_;m[23]=1;m[26]=m[31]=1;
    context_->UpdateSubresource(matrices_.get(),0,nullptr,m.data(),0,0);
}
void Gpu::draw(std::span<const Vertex> verts,Material& material,std::array<int,4> clip,unsigned stencil,unsigned reference,bool writeColor) {
    bool compatible=!pending_.empty()&&pendingMaterial_.program==material.program&&pendingMaterial_.blend==material.blend&&pendingClip_==clip&&pendingStencil_==stencil&&pendingReference_==reference&&pendingColor_==writeColor&&std::memcmp(pendingMaterial_.constants.data(),material.constants.data(),sizeof(material.constants))==0;
    for(int i=0;i<4&&compatible;i++){auto a=pendingMaterial_.textures[i],b=material.textures[i];compatible=(!a&&!b)||(a&&b&&a->view.get()==b->view.get());}
    if(!compatible||pending_.size()+verts.size()>65536)flush();
    if(pending_.empty()){pendingMaterial_=material;pendingClip_=clip;pendingStencil_=stencil;pendingReference_=reference;pendingColor_=writeColor;}
    pending_.insert(pending_.end(),verts.begin(),verts.end());
}
void Gpu::flush(){if(pending_.empty())return;submit(pending_,pendingMaterial_,pendingClip_,pendingStencil_,pendingReference_,pendingColor_);pending_.clear();}
void Gpu::forgetTexture(const Pixels* pixels){flush();textures_.erase(pixels);}
void Gpu::submit(std::span<const Vertex> verts,Material& material,std::array<int,4> clip,unsigned stencil,unsigned reference,bool writeColor) {
    if(verts.empty())return;auto& p=program(material.program);D3D11_MAPPED_SUBRESOURCE b{};check(context_->Map(vertices_.get(),0,D3D11_MAP_WRITE_DISCARD,0,&b),"MapStreamVertices");
    std::memcpy(b.pData,verts.data(),verts.size_bytes());context_->Unmap(vertices_.get(),0);
    auto buffer=vertices_.get();UINT stride=sizeof(Vertex),offset=0;context_->IASetVertexBuffers(0,1,&buffer,&stride,&offset);context_->IASetInputLayout(p.layout.get());context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(p.vertex.get(),nullptr,0);context_->PSSetShader(p.pixel.get(),nullptr,0);
    context_->GSSetShader(p.geometry.get(),nullptr,0);
    context_->UpdateSubresource(globals_.get(),0,nullptr,material.constants.data(),0,0);ID3D11Buffer* cb[]={matrices_.get(),globals_.get()};context_->VSSetConstantBuffers(0,2,cb);context_->PSSetConstantBuffers(0,2,cb);context_->GSSetConstantBuffers(0,2,cb);
    ID3D11ShaderResourceView* srvs[16]{};for(int i=0;i<4;i++)if(material.textures[i])srvs[i]=material.textures[i]->view.get();context_->PSSetShaderResources(0,16,srvs);
    ID3D11SamplerState* samplers[16]{};for(int i=0;i<16;i++)samplers[i]=p.samplers[i].get();context_->PSSetSamplers(0,16,samplers);
    D3D11_RECT rect{std::clamp(clip[0],0,viewportWidth_),std::clamp(clip[1],0,viewportHeight_),std::clamp(clip[2],0,viewportWidth_),std::clamp(clip[3],0,viewportHeight_)};
    if(rect.left>=rect.right||rect.top>=rect.bottom)return;context_->RSSetScissorRects(1,&rect);
    unsigned key=material.blend|(writeColor?0u:0x10000u);
    if(!blends_.contains(key)){D3D11_BLEND_DESC d{};auto& r=d.RenderTarget[0];r.BlendEnable=material.blend!=0x8000;r.RenderTargetWriteMask=writeColor?D3D11_COLOR_WRITE_ENABLE_ALL:0;r.BlendOp=r.BlendOpAlpha=D3D11_BLEND_OP_ADD;r.SrcBlendAlpha=D3D11_BLEND_ONE;r.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        r.SrcBlend=D3D11_BLEND_SRC_ALPHA;r.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
        switch(material.blend){case 1:r.SrcBlend=D3D11_BLEND_DEST_COLOR;r.DestBlend=D3D11_BLEND_SRC_ALPHA;break;case 2:r.DestBlend=D3D11_BLEND_ONE;break;case 4:r.SrcBlend=D3D11_BLEND_INV_SRC_COLOR;break;case 8:r.SrcBlend=D3D11_BLEND_INV_DEST_COLOR;r.DestBlend=D3D11_BLEND_ZERO;break;
        case 32:r.SrcBlend=D3D11_BLEND_INV_SRC_ALPHA;r.DestBlend=D3D11_BLEND_ONE;break;case 64:r.SrcBlend=D3D11_BLEND_ZERO;r.DestBlend=D3D11_BLEND_ONE;break;case 128:r.DestBlend=D3D11_BLEND_ZERO;break;case 256:r.SrcBlend=D3D11_BLEND_ONE;break;case 512:r.SrcBlend=D3D11_BLEND_ONE;r.DestBlend=D3D11_BLEND_SRC_ALPHA;break;case 1024:r.SrcBlend=D3D11_BLEND_ZERO;r.DestBlend=D3D11_BLEND_SRC_COLOR;break;}
        check(device_->CreateBlendState(&d,blends_[key].out()),"CreateNativeBlendState");}
    context_->OMSetBlendState(blends_[key].get(),nullptr,UINT_MAX);
    if(!stencils_.contains(stencil)){D3D11_DEPTH_STENCIL_DESC d{};d.DepthEnable=FALSE;d.StencilEnable=stencil!=0;d.StencilReadMask=d.StencilWriteMask=255;d.FrontFace.StencilFailOp=d.FrontFace.StencilDepthFailOp=D3D11_STENCIL_OP_KEEP;
        d.FrontFace.StencilPassOp=stencil==1?D3D11_STENCIL_OP_REPLACE:D3D11_STENCIL_OP_KEEP;d.FrontFace.StencilFunc=stencil==1?D3D11_COMPARISON_ALWAYS:stencil==3?D3D11_COMPARISON_NOT_EQUAL:D3D11_COMPARISON_EQUAL;d.BackFace=d.FrontFace;
        check(device_->CreateDepthStencilState(&d,stencils_[stencil].out()),"CreateNativeStencilState");}
    context_->OMSetDepthStencilState(stencils_[stencil].get(),reference);context_->Draw(UINT(verts.size()),0);draws++;
}
void Gpu::capture(Target& src,Target& dst){flush();ID3D11ShaderResourceView* nulls[16]{};context_->PSSetShaderResources(0,16,nulls);context_->CopyResource(dst.image.texture.get(),src.image.texture.get());}
std::shared_ptr<GpuTexture> Gpu::view(Target& t){auto image=std::make_shared<GpuTexture>();image->width=t.image.width;image->height=t.image.height;t.image.view->AddRef();*image->view.out()=t.image.view.get();return image;}
void Gpu::clearStencil(Target& t){flush();if(t.depthView)context_->ClearDepthStencilView(t.depthView.get(),D3D11_CLEAR_STENCIL,1,0);}
Bytes Gpu::readback(Target& t) {
    flush();
    D3D11_TEXTURE2D_DESC d{};t.image.texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Com<ID3D11Texture2D> staging;
    check(device_->CreateTexture2D(&d,nullptr,staging.out()),"CreateValidationReadback");context_->CopyResource(staging.get(),t.image.texture.get());D3D11_MAPPED_SUBRESOURCE b{};check(context_->Map(staging.get(),0,D3D11_MAP_READ,0,&b),"ReadValidationPixels");
    Bytes out(size_t(d.Width)*d.Height*4);for(UINT y=0;y<d.Height;y++)std::memcpy(out.data()+size_t(y)*d.Width*4,static_cast<uint8_t*>(b.pData)+y*b.RowPitch,d.Width*4);context_->Unmap(staging.get(),0);return out;
}
Bytes Gpu::screenshot(Target& t,bool straightAlpha){
    if(!straightAlpha)return readback(t);flush();
    int w=t.image.width,h=t.image.height;if(straightTarget_.image.width!=w||straightTarget_.image.height!=h)straightTarget_=target(w,h,false);
    float clear[4]{};begin(straightTarget_,clear);Material material;material.program=-2;material.blend=0x8000;material.textures[0]=view(t);
    std::array<Vertex,6> vertices{};int corners[]={0,1,2,2,1,3};for(int n=0;n<6;n++){int c=corners[n];vertices[n].position[0]=(c&1)*w;vertices[n].position[1]=(c>>1)*h;vertices[n].texcoord[2][0]=1;}
    draw(vertices,material,{0,0,w,h});return readback(straightTarget_);
}
void Gpu::present(Target& t,int w,int h) {
    if(!swapchain_||w<=0||h<=0)return;
    flush();activeTarget_=nullptr;
    DXGI_SWAP_CHAIN_DESC d{};swapchain_->GetDesc(&d);if(int(d.BufferDesc.Width)!=w||int(d.BufferDesc.Height)!=h){context_->OMSetRenderTargets(0,nullptr,nullptr);backbuffer_.reset();check(swapchain_->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,0),"ResizeReplaySwapchain");}
    if(!backbuffer_){Com<ID3D11Texture2D> b;check(swapchain_->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(b.out())),"GetSwapchainBuffer");check(device_->CreateRenderTargetView(b.get(),nullptr,backbuffer_.out()),"CreateSwapchainRTV");}
    auto view=backbuffer_.get();context_->OMSetRenderTargets(1,&view,nullptr);viewportWidth_=w;viewportHeight_=h;D3D11_VIEWPORT vp{0,0,float(w),float(h),0,1};context_->RSSetViewports(1,&vp);float black[]={0,0,0,1};context_->ClearRenderTargetView(view,black);
    std::array<float,32> m{};m[0]=m[5]=m[10]=m[15]=1;m[16]=2.f/w;m[19]=-1;m[21]=-2.f/h;m[23]=1;m[26]=m[31]=1;context_->UpdateSubresource(matrices_.get(),0,nullptr,m.data(),0,0);
    float scale=std::min(float(w)/t.image.width,float(h)/t.image.height),x=(w-t.image.width*scale)/2,y=(h-t.image.height*scale)/2;
    auto alias=std::make_shared<GpuTexture>();t.image.view->AddRef();*alias->view.out()=t.image.view.get();alias->width=t.image.width;alias->height=t.image.height;
    Material material;material.program=4;material.blend=0x8000;material.textures[0]=alias;material.constants[0]=t.image.width;material.constants[1]=t.image.height;material.constants[2]=material.constants[3]=1;
    std::array<Vertex,6> verts{};int corners[]={0,1,2,2,1,3};for(int i=0;i<6;i++){int c=corners[i];auto& v=verts[i];v.position[0]=x+(c&1)*t.image.width*scale;v.position[1]=y+(c>>1)*t.image.height*scale;v.texcoord[0][0]=c&1;v.texcoord[0][1]=c>>1;v.texcoord[2][0]=1;v.texcoord[7][0]=v.texcoord[7][1]=1;}
    draw(verts,material,{0,0,w,h});flush();swapchain_->Present(1,0);
}
}
