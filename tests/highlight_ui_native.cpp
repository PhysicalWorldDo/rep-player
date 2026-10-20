#define wWinMain RepHighlightUnusedEntry
#include "../src/app.cpp"
#undef wWinMain
#include <iostream>
template<class Predicate>void waitUi(Predicate predicate){auto end=Clock::now()+std::chrono::seconds(35);while(Clock::now()<end){if(predicate())return;MSG msg;for(int n=0;n<50&&PeekMessageW(&msg,nullptr,0,0,PM_REMOVE);n++){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(3);}throw rep::Error("highlight window check timed out");}
void saveWindow(HWND window,const std::filesystem::path& file){RECT r;GetClientRect(window,&r);auto dc=GetDC(window),memory=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);auto old=SelectObject(memory,bitmap);PrintWindow(window,memory,2);BITMAPINFO info{};info.bmiHeader.biSize=40;info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;rep::Bytes bytes(size_t(r.right)*r.bottom*4);SelectObject(memory,old);GetDIBits(memory,bitmap,0,r.bottom,bytes.data(),&info,DIB_RGB_COLORS);BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+40;header.bfSize=header.bfOffBits+bytes.size();std::ofstream output(file,std::ios::binary);output.write(reinterpret_cast<char*>(&header),sizeof(header));output.write(reinterpret_cast<char*>(&info.bmiHeader),40);output.write(reinterpret_cast<char*>(bytes.data()),bytes.size());DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(window,dc);}
int wmain(int argc,wchar_t** argv){if(argc!=3)return 2;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TREEVIEW_CLASSES|ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
    Application app;app.root=argv[2];app.client=argv[1];app.audioMuted=true;app.openPath=app.client/L"Replay/SkillReplay/ImperialKnight/SeaDragonTooth_VP1.rep";
    WNDCLASSW progress{};progress.lpfnWndProc=progressProcedure;progress.hInstance=GetModuleHandleW(nullptr);progress.lpszClassName=L"RepProgress";RegisterClassW(&progress);
    WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"RepHighlightNativeUi";RegisterClassW(&wc);
    auto window=CreateWindowW(wc.lpszClassName,L"REP IMG highlight validation",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,80,60,1280,790,nullptr,nullptr,wc.hInstance,&app);ShowWindow(window,SW_SHOWNOACTIVATE);
    std::cerr<<"stage opened\n"<<std::flush;waitUi([&]{return app.worker->status().phase==Phase::Playing;});std::cerr<<"stage playing\n"<<std::flush;app.worker->togglePause();waitUi([&]{return app.worker->status().phase==Phase::Paused;});
    app.worker->seekTime(1666);waitUi([&]{auto s=app.worker->status();return s.phase==Phase::Paused&&s.elapsed>=1666;});app.current=app.worker->status();app.updateImages(true);
    const std::string path="sprite/character/imperialknight/effect/seadragontooth/weaponswing.img";int index=-1;for(size_t i=0;i<app.imageRows.size();i++)if(app.imageRows[i].path==path)index=int(i);if(index<0)throw rep::Error("actual IMG row missing");
    auto click=[&](int x){ListView_EnsureVisible(app.imgList,index,FALSE);RECT row{};ListView_GetItemRect(app.imgList,index,&row,LVIR_BOUNDS);PostMessageW(app.imgList,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,(row.top+row.bottom)/2));PostMessageW(app.imgList,WM_LBUTTONUP,0,MAKELPARAM(x,(row.top+row.bottom)/2));};
    std::cerr<<"stage ready row "<<index<<'\n'<<std::flush;auto before=app.worker->status();click(110);std::cerr<<"stage clicked\n"<<std::flush;waitUi([&]{auto s=app.worker->status();return s.highlightedImage==path&&s.highlightAvailable&&s.presentationSerial>before.presentationSerial;});
    auto selected=app.worker->status();bool stable=selected.phase==Phase::Paused&&selected.ordinal==before.ordinal&&selected.elapsed==before.elapsed;
    saveWindow(window,app.root/L"player-highlight.bmp");click(110);waitUi([&]{auto s=app.worker->status();return s.highlightedImage.empty()&&!s.highlightAvailable&&s.presentationSerial>selected.presentationSerial;});
    click(10);waitUi([&]{auto s=app.worker->status();return s.phase==Phase::Paused&&s.hiddenImages.contains(path);});bool checkbox=app.worker->status().highlightedImage.empty();
    std::cout<<(stable&&checkbox?"PASS":"FAIL")<<" real native IMG row click, same-row clear, checkbox isolation and paused timeline\n";
    app.worker.reset();DestroyWindow(window);return stable&&checkbox?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
