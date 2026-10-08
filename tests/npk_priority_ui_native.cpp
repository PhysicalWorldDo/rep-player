// Exercise the real UI translation unit without adding a production test API.
#define wWinMain productionWinMain
#ifndef REP_NPK_UI_APP_SOURCE
#define REP_NPK_UI_APP_SOURCE "../src/app.cpp"
#endif
#include REP_NPK_UI_APP_SOURCE
#undef wWinMain
#include <iostream>

namespace {
constexpr int RefreshId=25;
void require(bool condition,const char* message){if(!condition)throw rep::Error(message);}
void pump(){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}}
template<class Predicate>
PlayerStatus waitFor(Application& app,Predicate predicate,const char* message){
    auto deadline=Clock::now()+std::chrono::seconds(15);
    while(Clock::now()<deadline){pump();auto status=app.worker->status();if(status.phase==Phase::Error)throw rep::Error(rep::utf8(status.message));if(predicate(status))return status;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    throw rep::Error(message);
}
uint32_t capture(Application& app){auto previous=app.worker->status().captureSerial;app.worker->captureFrame();return waitFor(app,[&](const auto& s){return s.captureSerial>previous;},"GPU frame capture did not finish").frameCrc;}
RECT rectangle(HWND window){RECT value{};require(GetWindowRect(window,&value),"Cannot read toolbar rectangle");return value;}
void screenshot(HWND window,const std::filesystem::path& path){
    RECT rect{};GetWindowRect(window,&rect);int width=rect.right-rect.left,height=rect.bottom-rect.top;
    auto screen=GetDC(window),memory=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* bytes=nullptr;
    auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bytes,nullptr,0);require(bitmap&&memory,"Cannot allocate screenshot bitmap");auto previous=SelectObject(memory,bitmap);
    bool captured=PrintWindow(window,memory,2);BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+width*height*4;
    if(captured){std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(&header),sizeof(header));output.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));output.write(static_cast<const char*>(bytes),width*height*4);}
    SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(window,screen);require(captured,"PrintWindow did not capture the owned UI");
}
void command(Application& app,int id){SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(app.window,id)));}
void checkToolbar(Application& app,const std::filesystem::path& directory){
    auto refresh=GetDlgItem(app.window,RefreshId);require(refresh,"native UI is missing the authorized Refresh NPK toolbar button (ID 25)");
    SetWindowPos(app.window,nullptr,0,0,1040,720,SWP_NOMOVE|SWP_NOZORDER);pump();
    for(bool english:{false,true}){
        command(app,english?LanguageEn:LanguageZh);auto label=controlText(refresh);require(english?label==L"Refresh NPK":(label==L"刷新 NPK"||label==L"刷新NPK"),"Refresh NPK label does not match the selected language");
        std::vector<HWND> controls{app.clientTitle,app.pathLabel,refresh,app.canvasButton,app.openButton,app.choose,app.zhButton,app.enButton};
        for(size_t i=1;i<controls.size();++i){auto left=rectangle(controls[i-1]),right=rectangle(controls[i]);require(left.right<=right.left,"Minimum-width toolbar controls overlap");require(right.bottom>right.top&&right.right>right.left,"Toolbar contains a zero-size control");}
        RECT client{};GetClientRect(app.window,&client);auto last=rectangle(app.enButton);POINT edge{last.right,last.bottom};ScreenToClient(app.window,&edge);require(edge.x<=client.right&&edge.y<=client.bottom,"Toolbar extends outside the minimum window");
        HDC dc=GetDC(refresh);auto old=SelectObject(dc,app.font);SIZE size{};GetTextExtentPoint32W(dc,label.c_str(),int(label.size()),&size);SelectObject(dc,old);ReleaseDC(refresh,dc);auto bounds=rectangle(refresh);require(size.cx+8<=bounds.right-bounds.left,"Refresh NPK label is clipped");
        screenshot(app.window,directory/(english?L"toolbar_en.bmp":L"toolbar_zh.bmp"));
    }
}
void exportPng(Application& app,const std::filesystem::path& directory,const wchar_t* name){
    rep::ExportOptions options;options.format=rep::ExportFormat::Png;options.fps=30;options.audio=false;options.alpha=true;options.outputDirectory=directory/L"exports";options.fileName=name;options.hiddenImages=app.hiddenImages;app.startExport(options);
    auto deadline=Clock::now()+std::chrono::seconds(15);bool done=false,failed=false;std::wstring message;
    while(Clock::now()<deadline){pump();{std::lock_guard lock(app.exportMutex);done=app.exportDone;failed=app.exportFailed;message=app.exportMessage;}if(done)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    require(done,"PNG export did not finish");if(failed)throw rep::Error("PNG export failed: "+rep::utf8(message));if(app.exportThread.joinable())app.exportThread.join();
}
}

int wmain(int argc,wchar_t** argv){
    if(argc!=5)return 2;HWND window=nullptr;std::unique_ptr<Application> app;
    try{
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TREEVIEW_CLASSES|ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
        auto directory=std::filesystem::path(argv[2]),client=std::filesystem::path(argv[3]);std::filesystem::create_directories(directory);
        app=std::make_unique<Application>();app->root=directory;if(std::wstring_view(argv[1])!=L"empty")app->client=client;
        WNDCLASSEXW type{};type.cbSize=sizeof(type);type.lpfnWndProc=procedure;type.hInstance=GetModuleHandleW(nullptr);type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));type.lpszClassName=L"NpkPriorityUiProbe";RegisterClassExW(&type);
        window=CreateWindowExW(0,type.lpszClassName,L"NPK priority UI regression",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,0,0,1040,720,nullptr,nullptr,type.hInstance,app.get());require(window,"Cannot create the actual owned player UI");ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);
        checkToolbar(*app,directory);
        if(std::wstring_view(argv[1])==L"empty"){
            require(!IsWindowEnabled(GetDlgItem(window,RefreshId)),"Refresh NPK must be disabled without a selected client");std::cout<<"{\"toolbar\":true,\"empty_client_disabled\":true}\n";
        }else{
            auto replay=client/L"Replay"/L"refresh.rep";app->openSpecified(replay);waitFor(*app,[](const auto& s){return s.phase==Phase::Ended;},"Initial replay did not end");
            rep::CanvasSettings settings;settings.factor=1.5;app->applyCanvasSettings(settings);app->hiddenImages={"sprite/not_used.img"};app->worker->setHiddenImages(app->hiddenImages);app->setAudioVolume(37);app->toggleAudioMute();
            waitFor(*app,[](const auto& s){return s.canvasWidth==24&&s.canvasHeight==24&&s.hiddenImages.contains("sprite/not_used.img")&&s.audioMuted&&s.audioVolume>.36f&&s.audioVolume<.38f;},"Existing UI settings were not applied");
            auto baseCrc=capture(*app);std::filesystem::copy_file(argv[4],client/L"ImagePacks2"/L"0_patch.NPK");
            // A disk addition alone must not give export a newer rule than preview.
            exportPng(*app,directory,L"before_refresh");require(capture(*app)==baseCrc,"Adding a patch changed preview without Refresh NPK");
            command(*app,RefreshId);require(!IsWindowEnabled(app->exportButton),"Export remains enabled while NPK refresh is pending");
            waitFor(*app,[](const auto& s){return s.phase==Phase::Ended;},"Refreshed replay did not finish");
            uint32_t patchCrc=0;auto deadline=Clock::now()+std::chrono::seconds(10);while(Clock::now()<deadline){auto s=app->worker->status();if(s.phase==Phase::Ended){patchCrc=capture(*app);if(patchCrc!=baseCrc)break;}pump();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            require(patchCrc!=baseCrc,"Real Refresh NPK command did not apply the newly added patch");
            auto status=app->worker->status();require(status.canvasWidth==24&&status.canvasHeight==24&&status.hiddenImages==app->hiddenImages&&status.audioMuted&&status.audioVolume>.36f&&status.audioVolume<.38f,"Refresh NPK lost canvas, hidden images or preview audio settings");
            app->current=status;app->updateImages(true);require(std::any_of(app->imageRows.begin(),app->imageRows.end(),[](const auto& row){return row.path=="sprite/test/frame.img"&&row.drawn;}),"Refreshed IMG panel does not describe the rendered image");
            auto serial=status.captureSerial;command(*app,PreviousFrame);auto previous=waitFor(*app,[&](const auto& s){return s.phase==Phase::Paused&&s.captureSerial>serial;},"Previous frame did not finish");require(previous.frameCrc==patchCrc,"Previous frame used a different NPK winner");
            serial=previous.captureSerial;command(*app,NextFrame);auto next=waitFor(*app,[&](const auto& s){return s.phase==Phase::Paused&&s.captureSerial>serial;},"Next frame did not finish");require(next.frameCrc==patchCrc,"Next frame used a different NPK winner");
            exportPng(*app,directory,L"after_refresh");
            std::cout<<"{\"toolbar\":true,\"refresh_command\":true,\"pending_export_disabled\":true,\"settings_preserved\":true,\"img_panel\":true,\"frame_step\":true,\"base_crc\":"<<baseCrc<<",\"patch_crc\":"<<patchCrc<<"}\n";
        }
        SendMessageW(window,WM_CLOSE,0,0);window=nullptr;app.reset();CoUninitialize();return 0;
    }catch(const std::exception& error){if(window&&IsWindow(window))SendMessageW(window,WM_CLOSE,0,0);app.reset();std::cerr<<error.what()<<'\n';CoUninitialize();return 1;}
}
