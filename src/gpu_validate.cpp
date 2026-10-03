#include "gpu.hpp"
#include "bindings.hpp"
#include "fonts.hpp"
#include "movies.hpp"
#include "engine.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>

static void shaderFixture(rep::Gpu& gpu,rep::Binder& binder,rep::Replay* replay,const std::filesystem::path& inputPath,const std::filesystem::path& outputPath,bool debug=false){
    auto input=rep::readFile(inputPath);rep::Reader r(input);auto magic=r.take(4);if(std::memcmp(magic.data(),"SFX1",4))throw rep::Error("invalid shader fixture");
    rep::Effect e;e.present=true;e.type=r.get<uint32_t>();rep::ShaderContext c;c.version=r.get<uint32_t>();c.minor=r.get<uint32_t>();c.width=r.get<uint32_t>();c.height=r.get<uint32_t>();c.channel=r.get<uint32_t>();
    auto nf=r.get<uint32_t>(),nb=r.get<uint32_t>(),external=r.get<uint32_t>();c.actorFacing=r.get<uint32_t>();c.stoneMode=r.get<uint32_t>();
    float color[4];for(auto& x:color)x=r.get<float>();while(nf--)e.floats.push_back(r.get<float>());while(nb--)e.bits.push_back(r.get<uint32_t>());r.end();
    auto pixels=std::make_shared<rep::Pixels>();pixels->width=c.width;pixels->height=c.height;pixels->rgba.resize(size_t(c.width)*c.height*4);
    for(int y=0;y<c.height;y++)for(int x=0;x<c.width;x++){size_t n=(y*c.width+x)*4;pixels->rgba[n]=x*17;pixels->rgba[n+1]=y*17;pixels->rgba[n+2]=((x+y)%2)*255;pixels->rgba[n+3]=((x/4)%2)?128:255;}
    auto source=gpu.texture(pixels);c.canvas=source;for(int i=0;i<4;i++)if(external&(1u<<i))c.external[i]=source;c.multiColor={1,.5f,.25f,1};
    binder.setReplay(replay);auto material=binder.bind(&e,source,c);material.blend=0x8000;
    auto target=gpu.target(c.width,c.height);float transparent[4]{};gpu.begin(target,transparent);
    std::array<rep::Vertex,6> verts{};int corners[]={0,1,2,2,1,3};for(int n=0;n<6;n++){auto& v=verts[n];int corner=corners[n];v.position[0]=(corner&1)*c.width;v.position[1]=(corner>>1)*c.height;
        std::copy(color,color+4,v.color);v.texcoord[0][0]=corner&1;v.texcoord[0][1]=corner>>1;v.texcoord[1][0]=c.channel?float(c.channel):material.elapsed;v.texcoord[1][1]=material.row;v.texcoord[2][0]=1;
        for(int k=0;k<6;k++)v.texcoord[3+k/2][k%2]=material.parameters[k];v.texcoord[7][0]=v.texcoord[7][1]=1;}
    auto before=gpu.draws;gpu.draw(verts,material,{0,0,c.width,c.height});auto rgba=gpu.readback(target);std::ofstream out(outputPath,std::ios::binary);out.write(reinterpret_cast<const char*>(rgba.data()),rgba.size());
    if(debug){std::ofstream trace(std::filesystem::path(outputPath.wstring()+L".bindings.json"));trace<<"{\"constants\":[";for(size_t n=0;n<material.constants.size();n++){if(n)trace<<',';trace<<material.constants[n];}trace<<"],\"textures\":[";bool first=true;
        for(int slot=0;slot<4;slot++)if(auto texture=material.textures[slot]){D3D11_TEXTURE2D_DESC d{};texture->texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;rep::Com<ID3D11Texture2D> staging;rep::check(gpu.device()->CreateTexture2D(&d,nullptr,staging.out()),"ValidationTextureCopy");gpu.context()->CopyResource(staging.get(),texture->texture.get());D3D11_MAPPED_SUBRESOURCE mapped{};rep::check(gpu.context()->Map(staging.get(),0,D3D11_MAP_READ,0,&mapped),"ValidationTextureMap");int size=d.Format==DXGI_FORMAT_R32G32B32A32_FLOAT?16:4;
            std::ofstream bytes(std::filesystem::path(outputPath.wstring()+L".t"+std::to_wstring(slot)+L".raw"),std::ios::binary);for(UINT y=0;y<d.Height;y++)bytes.write(static_cast<const char*>(mapped.pData)+size_t(y)*mapped.RowPitch,d.Width*size);gpu.context()->Unmap(staging.get(),0);if(!first)trace<<',';first=false;trace<<"{\"slot\":"<<slot<<",\"width\":"<<d.Width<<",\"height\":"<<d.Height<<",\"float\":"<<(size==16?"true":"false")<<"}";}
        trace<<"]}";
    }
    gpu.forgetTexture(pixels.get());
    std::cout<<"{\"type\":"<<e.type<<",\"program\":"<<material.program<<",\"draws\":"<<gpu.draws-before<<",\"parameters\":[";for(int n=0;n<6;n++){if(n)std::cout<<',';if(std::isfinite(material.parameters[n]))std::cout<<material.parameters[n];else std::cout<<"null";}std::cout<<"]}"<<std::endl;
}
int wmain(int argc,wchar_t** argv){try{
    wchar_t name[32768];GetModuleFileNameW(nullptr,name,32768);auto root=std::filesystem::path(name).parent_path().parent_path();rep::Gpu gpu(root/L"assets"/L"shaders");
    if(argc>1&&std::wstring_view(argv[1])==L"--smoke")std::cout<<"{\"programs_created\":"<<gpu.createAllPrograms()<<",\"hardware\":true,\"adapter\":\""<<gpu.adapter<<"\"}\n";
    else if(argc==5&&std::wstring_view(argv[1])==L"--frame-step"){
        rep::Assets assets(argv[3]);rep::Replay replay(argv[2]),scan(argv[2]),referenceReplay(argv[2]);auto inspection=rep::inspectReplayImages(scan);
        rep::Executor executor(gpu,assets,root/L"runtime"/L"cache"),reference(gpu,assets,root/L"runtime"/L"cache");executor.setTransparent(true);executor.attach(replay);executor.setInspection(inspection);reference.setTransparent(true);
        rep::Playback playback;playback.attach(replay,executor);playback.start();std::istringstream actions(rep::utf8(argv[4]));std::string action;bool first=true;
        std::cout<<"{\"frame_count\":"<<inspection.scenes<<",\"snapshots\":[";
        while(std::getline(actions,action,',')){
            bool changed=false;if(action=="next")changed=playback.step(1);else if(action=="prev")changed=playback.step(-1);
            else if(action.starts_with("select="))changed=playback.select(std::stoll(action.substr(7)));
            else if(action=="hide"){executor.setHiddenImages({"sprite/test/frame.img"});changed=playback.refresh();}
            else if(action=="resume"){playback.resume();Sleep(25);}else if(action=="stop")playback.stop();else throw rep::Error("unknown frame-step action");
            auto pixels=gpu.readback(executor.output());auto calls=executor.currentImages();uint32_t expected=rep::crc(pixels);
            if(inspection.scenes){referenceReplay.rewind();reference.attach(referenceReplay);reference.setHiddenImages(executor.hiddenImages());rep::Scene frame;while(referenceReplay.next(frame)){reference.execute(frame);if(frame.ordinal>=playback.ordinal())break;}expected=rep::crc(gpu.readback(reference.output()));}
            if(!first)std::cout<<',';first=false;std::cout<<"{\"ordinal\":"<<playback.ordinal()<<",\"timestamp\":"<<playback.timestamp()<<",\"elapsed\":"<<playback.elapsedMilliseconds()<<",\"paused\":"<<(playback.paused()?"true":"false")<<",\"ended\":"<<(playback.ended()?"true":"false")<<",\"changed\":"<<(changed?"true":"false")<<",\"crc\":"<<rep::crc(pixels)<<",\"reference_crc\":"<<expected<<",\"hidden_count\":"<<executor.hiddenImages().size()<<",\"images\":[";
            bool firstImage=true;for(auto& call:calls){if(!firstImage)std::cout<<',';firstImage=false;std::cout<<"{\"path\":\""<<call.path<<"\",\"hidden\":"<<(call.hidden?"true":"false")<<",\"drawn\":"<<(call.drawn?"true":"false")<<'}';}std::cout<<"]}";
        }std::cout<<"]}\n";
    }
    else if((argc>=5&&argc<=7)&&std::wstring_view(argv[1])==L"--engine-contract"){
        auto folder=std::filesystem::path(argv[4]);rep::Assets assets(argv[3]);rep::Replay replay(argv[2]),inspectionReplay(argv[2]);auto inspection=rep::inspectReplayImages(inspectionReplay);rep::Executor executor(gpu,assets,root/L"runtime"/L"cache");executor.setTransparent(true);executor.attach(replay);executor.setInspection(inspection);rep::Playback playback;playback.attach(replay,executor);playback.start();playback.select(0);if(argc==7)playback.select(std::stoll(argv[6]));playback.pause();auto pausedBefore=playback.elapsedMilliseconds();Sleep(45);auto pausedAfter=playback.elapsedMilliseconds();
        auto beforeTimestamp=playback.timestamp();auto visible=gpu.screenshot(executor.output(),true);executor.setHiddenImages({argc>=6?rep::utf8(argv[5]):"sprite/test/frame.img"});playback.refresh();auto hidden=gpu.screenshot(executor.output(),true);auto current=executor.currentImages();auto afterTimestamp=playback.timestamp();executor.setHiddenImages({});playback.refresh();auto restored=gpu.screenshot(executor.output(),true);playback.resume();Sleep(30);auto resumed=playback.elapsedMilliseconds();playback.seek(0);auto backward=playback.timestamp();playback.select(500);auto all=executor.allImages();
        auto save=[&](const wchar_t* name,const rep::Bytes& bytes){std::ofstream file(folder/name,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());};save(L"visible.rgba",visible);save(L"hidden.rgba",hidden);
        auto images=[&](const std::vector<rep::ImgCall>& values){std::cout<<'[';bool first=true;for(auto& call:values){if(!first)std::cout<<',';first=false;std::cout<<"{\"path\":\""<<call.path<<"\",\"frame\":"<<call.frame<<",\"count\":"<<call.count<<",\"role\":\""<<call.role<<"\",\"drawn\":"<<(call.drawn?"true":"false")<<",\"hidden\":"<<(call.hidden?"true":"false")<<",\"registered\":"<<(call.registered?"true":"false")<<'}';}std::cout<<']';};
        std::cout<<"{\"visible_crc\":"<<rep::crc(visible)<<",\"hidden_crc\":"<<rep::crc(hidden)<<",\"restored_crc\":"<<rep::crc(restored)<<",\"paused_before\":"<<pausedBefore<<",\"paused_after\":"<<pausedAfter<<",\"resumed_elapsed\":"<<resumed<<",\"refresh_timestamp_before\":"<<beforeTimestamp<<",\"refresh_timestamp_after\":"<<afterTimestamp<<",\"backward_timestamp\":"<<backward<<",\"duration\":"<<inspection.durationMilliseconds<<",\"inspection_scenes\":"<<inspection.scenes<<",\"exact_eof\":"<<(playback.ended()?"true":"false")<<",\"current_images\":";images(current);std::cout<<",\"all_images\":";images(all);std::cout<<",\"inspection_images\":";images(inspection.images);std::cout<<"}\n";
    }
    else if(argc==3&&std::wstring_view(argv[1])==L"--movie") {
        rep::Movies movies(L"D:\\115us\\client",root/L"runtime"/L"cache"/L"movies");auto first=movies.frame(rep::utf8(argv[2]),1,0);if(!first)throw rep::Error("movie missing");auto a=rep::crc(first->texture->rgba);auto b=rep::crc(movies.frame(rep::utf8(argv[2]),1,300)->texture->rgba);auto c=rep::crc(movies.frame(rep::utf8(argv[2]),1,0)->texture->rgba);auto info=movies.info(1);
        std::cout<<"{\"width\":"<<info.width<<",\"height\":"<<info.height<<",\"frames\":"<<info.frames<<",\"rate\":"<<info.rate<<",\"first_crc32\":"<<a<<",\"later_crc32\":"<<b<<",\"rewind_crc32\":"<<c<<"}\n";
    }
    else if(argc==4&&std::wstring_view(argv[1])==L"--select"){
        rep::Assets assets(L"D:\\115us\\client\\ImagePacks2");rep::Replay replay(argv[2]);rep::Executor executor(gpu,assets,root/L"runtime"/L"cache");executor.attach(replay);rep::Playback playback;playback.attach(replay,executor);playback.start();std::istringstream times(rep::utf8(argv[3]));std::string value;std::vector<int> timestamps;std::vector<bool> changed;
        while(std::getline(times,value,',')){changed.push_back(playback.select(std::stoll(value)));timestamps.push_back(playback.timestamp());}
        std::cout<<"{\"timestamps\":[";for(size_t n=0;n<timestamps.size();n++){if(n)std::cout<<',';std::cout<<timestamps[n];}std::cout<<"],\"changed\":[";for(size_t n=0;n<changed.size();n++){if(n)std::cout<<',';std::cout<<(changed[n]?"true":"false");}std::cout<<"],\"selected\":"<<playback.selected<<",\"skipped\":"<<playback.skipped<<",\"ended\":"<<(playback.ended()?"true":"false");playback.start();playback.select(0);std::cout<<",\"rewind_timestamp\":"<<playback.timestamp()<<"}\n";
    }
    else if((argc==3||argc==4)&&std::wstring_view(argv[1])==L"--text-layout"){
        rep::Replay replay(argv[2]);rep::Scene scene;rep::Fonts fonts(argc==4?std::filesystem::path(argv[3]):std::filesystem::path(L"D:\\115us\\client\\Fonts"));std::vector<rep::GlyphQuad> quads;
        if(replay.next(scene)){rep::Reader aux(scene.aux);for(auto id:scene.ids)if(auto command=replay.command(id))for(const auto& i:command->instructions){float x=0,y=0;if(i.auxBytes){x=aux.get<int16_t>();y=aux.get<int16_t>();}if(i.opcode==5||i.opcode==48){if(i.opcode==48&&replay.header.minor<7){x=rep::at<float>(i.native,44);y=rep::at<float>(i.native,48);}auto text=fonts.prepare(i,replay,x,y);quads.insert(quads.end(),text.begin(),text.end());}}aux.end();}
        std::cout<<'[';bool first=true;for(const auto& q:quads){if(!first)std::cout<<',';first=false;std::cout<<"{\"x\":"<<q.x<<",\"y\":"<<q.y<<",\"scale\":"<<q.scale<<",\"width\":"<<q.frame->width<<",\"height\":"<<q.frame->height<<",\"rect\":[";for(int n=0;n<4;n++){if(n)std::cout<<',';std::cout<<q.frame->rect[n];}std::cout<<"],\"channel\":"<<q.channel<<",\"gradient\":"<<(q.gradient?"true":"false")<<"}";}std::cout<<"]\n";
    }
    else if(argc==2&&std::wstring_view(argv[1])==L"--font-plane-atlas"){
        rep::Atlas atlas;rep::Glyph large,small,border;large.width=large.height=1022;large.coverage.assign(1022*1022,255);auto low=large;low.coverage.assign(1022*1022,17);auto a=atlas.registerGlyph("full",large,low,1,1);small.width=border.width=4;small.height=border.height=1;small.coverage={0,85,170,255};border.coverage.assign(4,119);auto b=atlas.registerGlyph("BA",small,border,1,1);atlas.refresh();auto texture=gpu.texture(atlas.pixels);rep::Assets assets(L"D:\\115us\\client\\ImagePacks2");rep::Binder binder(gpu,assets);auto target=gpu.target(4,4);float clear[4]{};gpu.begin(target,clear);
        for(int plane=1;plane<=4;plane++){rep::ShaderContext context;context.channel=plane;auto material=binder.bind(nullptr,texture,context);material.blend=0x8000;std::array<rep::Vertex,6> vertices{};int corners[]={0,1,2,2,1,3};for(int n=0;n<6;n++){int c=corners[n];auto& v=vertices[n];v.position[0]=(c&1)*4;v.position[1]=plane-1+(c>>1);v.texcoord[0][0]=float(b->normal[c&1?2:0])/atlas.pixels->width;v.texcoord[0][1]=float(b->normal[c>>1?3:1])/atlas.pixels->height;v.texcoord[1][0]=plane;v.texcoord[2][0]=1;v.texcoord[7][0]=v.texcoord[7][1]=1;}gpu.draw(vertices,material,{0,0,4,4});}
        auto pixels=gpu.readback(target);std::cout<<"{\"formats\":["<<a->format<<','<<b->format<<"],\"alpha\":[";for(int y=0;y<4;y++){if(y)std::cout<<',';std::cout<<'[';for(int x=0;x<4;x++){if(x)std::cout<<',';std::cout<<int(pixels[(y*4+x)*4+3]);}std::cout<<']';}std::cout<<"]}\n";
    }
    else if(argc>=4&&std::wstring_view(argv[1])==L"--consume") {
        auto begin=std::chrono::steady_clock::now();rep::Assets assets(argc>=6?std::filesystem::path(argv[5]):std::filesystem::path(L"D:\\115us\\client\\ImagePacks2"));rep::Replay replay(argv[2]);rep::Executor executor(gpu,assets,root/L"runtime"/L"cache");executor.attach(replay);executor.prepare();auto ready=std::chrono::steady_clock::now();rep::Scene scene;int32_t last=0;uint64_t bytes=0;double maxFrame=0;rep::Bytes snapshot;
        while(replay.next(scene)){auto before=std::chrono::steady_clock::now();executor.execute(scene);gpu.flush();maxFrame=std::max(maxFrame,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count());last=scene.timestamp;bytes+=scene.aux.size();if(argc>=7&&scene.ordinal==uint64_t(std::stoull(argv[6])))snapshot=gpu.readback(executor.output());}
        auto pixels=gpu.readback(executor.output());double load=std::chrono::duration<double>(ready-begin).count(),seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-ready).count();std::ofstream out{std::filesystem::path(argv[3])};auto& s=executor.statistics;
        out<<"{\"version\":"<<replay.version/10.<<",\"minor\":"<<replay.header.minor<<",\"width\":"<<replay.header.width()<<",\"height\":"<<replay.header.height()<<",\"scenes\":"<<s.scenes<<",\"gpu_draws\":"<<gpu.draws<<",\"exact_eof\":true,\"prepare_seconds\":"<<load<<",\"execute_seconds\":"<<seconds<<",\"max_cpu_frame_ms\":"<<maxFrame<<",\"last_timestamp\":"<<last<<",\"aux_bytes\":"<<bytes<<",\"pixel_crc32\":"<<rep::crc(pixels);
        out<<",\"null_resources\":"<<s.nullResources<<",\"null_captures\":"<<s.nullCaptures<<",\"fallbacks\":"<<assets.fallbacks<<",\"actor_pool_updates\":"<<s.actorPoolUpdates<<",\"camera_updates\":"<<s.cameraUpdates<<",\"audio_events\":"<<s.audioEvents<<",\"phantom_pushes\":"<<s.phantomPushes<<",\"grid_cells\":"<<s.gridCells<<",\"stencil_draws\":"<<s.stencilDraws<<",\"sampler_draws\":"<<s.samplerDraws<<",\"null_caches\":"<<s.nullCaches;
        out<<",\"shader_counts\":[";for(size_t n=0;n<s.effects.size();n++){if(n)out<<',';out<<s.effects[n];}
        out<<"],\"opcodes\":[";for(size_t n=0;n<s.opcodes.size();n++){if(n)out<<',';out<<s.opcodes[n];}out<<"]}\n";
        if(argc>=5){auto& selected=snapshot.empty()?pixels:snapshot;std::ofstream raw(std::filesystem::path(argv[4]),std::ios::binary);raw.write(reinterpret_cast<const char*>(selected.data()),selected.size());}
        std::cout<<"{\"scenes\":"<<s.scenes<<",\"prepare_seconds\":"<<load<<",\"execute_seconds\":"<<seconds<<"}\n";
    }
    else if((argc==10||argc==11)&&std::wstring_view(argv[1])==L"--font") {
        int id=std::stoi(argv[2]),height=std::stoi(argv[3]),weight=std::stoi(argv[4]),slant=std::stoi(argv[5]),outline=std::stoi(argv[6]),padding=std::stoi(argv[7]),scale=std::stoi(argv[8]);
        rep::Rasterizer rasterizer(argc==11?std::filesystem::path(argv[10]):std::filesystem::path(L"D:\\115us\\client\\Fonts"));rep::Atlas atlas;std::cout<<"{\"metrics\":[";bool first=true;
        for(auto ch:std::wstring(argv[9])){auto normal=rasterizer.glyph(id,height,weight,slant,0,ch),border=rasterizer.glyph(id,height,weight,slant,outline,ch);atlas.registerGlyph(std::to_string(ch),*normal,*border,padding,scale);
            if(!first)std::cout<<',';first=false;std::cout<<'['<<normal->left<<','<<normal->top<<','<<normal->advance<<']';}
        atlas.refresh();std::cout<<"],\"atlas_crc32\":"<<rep::crc(atlas.pixels->rgba)<<"}\n";
    }
    else if(argc>3&&(std::wstring_view(argv[1])==L"--shader-fixture"||std::wstring_view(argv[1])==L"--shader-fixture-debug")) {
        rep::Assets assets(L"D:\\115us\\client\\ImagePacks2");std::unique_ptr<rep::Replay> replay;if(argc>=5)replay=std::make_unique<rep::Replay>(argv[4]);rep::Binder binder(gpu,assets,replay.get());shaderFixture(gpu,binder,replay.get(),argv[2],argv[3],std::wstring_view(argv[1])==L"--shader-fixture-debug");
    }
    else if(argc==3&&std::wstring_view(argv[1])==L"--shader-server"){
        rep::Assets assets(L"D:\\115us\\client\\ImagePacks2");rep::Replay replay(argv[2]);rep::Binder binder(gpu,assets,&replay);std::string input,output;
        while(std::getline(std::cin,input)&&std::getline(std::cin,output))shaderFixture(gpu,binder,&replay,rep::wide(input),rep::wide(output));
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
