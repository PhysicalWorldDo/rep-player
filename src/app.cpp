#include "ui_playback.hpp"
#include "catalog.hpp"
#include "export.hpp"
#include "ui_widgets.hpp"
#include "image_categories.hpp"
#include "runtime_paths.hpp"
#include <commctrl.h>
#include <windowsx.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <limits>

namespace {
using namespace rep::ui;
constexpr COLORREF Background=RGB(31,32,34),Surface=RGB(41,42,45),Panel=RGB(36,37,40),Text=RGB(223,225,229),Muted=RGB(145,151,161),Yellow=RGB(234,208,91),Selected=RGB(65,64,57);
constexpr int ImagePanelMinimum=271;
enum Id {Choose=1,Replay=2,Stop=3,Playlist=4,Canvas=5,StatusLabel=6,PlayPause=7,Search=8,LanguageZh=9,LanguageEn=10,Export=11,CurrentImages=12,AllImages=13,ShowAll=14,Images=15,PreviousFrame=16,NextFrame=17,RemoveBackground=18,RemoveMonsters=19,RemoveCharacters=20,OpenRep=21,CanvasOptions=22,AudioMute=23,AudioVolume=24,RefreshNpk=25};
enum CanvasId {ModeMultiple=301,ModeSize=302,ModePadding=303,FactorOne=310,FactorOneHalf=311,FactorTwo=312,FactorThree=313,CanvasWidth=320,CanvasHeight=321,CanvasLeft=322,CanvasTop=323,CanvasRight=324,CanvasBottom=325,CanvasResult=330,CanvasOrigin=331,CanvasError=332,CanvasOriginal=333,CanvasApply=334,CanvasRestore=335};
std::wstring controlText(HWND w){int n=GetWindowTextLengthW(w);std::wstring s(n+1,L'\0');GetWindowTextW(w,s.data(),n+1);s.resize(n);return s;}
void text(HWND w,const std::wstring& s){if(controlText(w)!=s)SetWindowTextW(w,s.c_str());}
std::wstring timeText(int64_t ms){ms=std::max<int64_t>(0,ms);std::wostringstream s;s<<std::setfill(L'0')<<std::setw(2)<<ms/60000<<L":"<<std::setw(2)<<ms/1000%60<<L"."<<std::setw(3)<<ms%1000;return s.str();}
bool chooseDirectory(HWND owner,const std::filesystem::path& initial,const wchar_t* title,std::filesystem::path& result){
    rep::Com<IFileDialog> dialog;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_IFileDialog,reinterpret_cast<void**>(dialog.out()))))return false;
    DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);dialog->SetTitle(title);
    rep::Com<IShellItem> initialItem;if(SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(),nullptr,IID_IShellItem,reinterpret_cast<void**>(initialItem.out()))))dialog->SetFolder(initialItem.get());
    if(FAILED(dialog->Show(owner)))return false;rep::Com<IShellItem> item;if(FAILED(dialog->GetResult(item.out())))return false;PWSTR path=nullptr;if(FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path)))return false;result=path;CoTaskMemFree(path);return true;
}
bool chooseRepFile(HWND owner,const std::filesystem::path& initial,const wchar_t* title,std::filesystem::path& result){
    rep::Com<IFileDialog> dialog;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_IFileDialog,reinterpret_cast<void**>(dialog.out()))))return false;
    DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_STRICTFILETYPES);dialog->SetTitle(title);
    COMDLG_FILTERSPEC filter{L"REP (*.rep)",L"*.rep"};dialog->SetFileTypes(1,&filter);dialog->SetDefaultExtension(L"rep");
    rep::Com<IShellItem> initialItem;if(SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(),nullptr,IID_IShellItem,reinterpret_cast<void**>(initialItem.out()))))dialog->SetFolder(initialItem.get());
    if(FAILED(dialog->Show(owner)))return false;rep::Com<IShellItem> item;if(FAILED(dialog->GetResult(item.out())))return false;PWSTR path=nullptr;if(FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path)))return false;result=path;CoTaskMemFree(path);return true;
}
void drawButton(const DRAWITEMSTRUCT& d,bool active){
    COLORREF fill=(d.itemState&ODS_SELECTED)?RGB(72,73,77):(active?Selected:Panel);SetDCBrushColor(d.hDC,fill);FillRect(d.hDC,&d.rcItem,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));SetDCBrushColor(d.hDC,RGB(69,70,75));FrameRect(d.hDC,&d.rcItem,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));SetBkMode(d.hDC,TRANSPARENT);COLORREF foreground=active?Yellow:((d.itemState&ODS_DISABLED)?Muted:Text);SetTextColor(d.hDC,foreground);RECT r=d.rcItem;
    if(d.CtlID==AudioMute){
        int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;SetDCBrushColor(d.hDC,foreground);
        RECT body{x-10,y-3,x-6,y+3};FillRect(d.hDC,&body,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        auto oldPen=SelectObject(d.hDC,GetStockObject(NULL_PEN)),oldBrush=SelectObject(d.hDC,GetStockObject(DC_BRUSH));
        POINT cone[]={{x-6,y-3},{x,y-7},{x,y+7},{x-6,y+3}};Polygon(d.hDC,cone,4);
        auto pen=CreatePen(PS_SOLID,2,foreground);SelectObject(d.hDC,pen);
        if(active){MoveToEx(d.hDC,x+4,y-4,nullptr);LineTo(d.hDC,x+12,y+4);MoveToEx(d.hDC,x+4,y+4,nullptr);LineTo(d.hDC,x+12,y-4);}
        else{POINT inner[]={{x+4,y-3},{x+6,y},{x+4,y+3}},outer[]={{x+7,y-6},{x+10,y-3},{x+11,y},{x+10,y+3},{x+7,y+6}};Polyline(d.hDC,inner,3);Polyline(d.hDC,outer,5);}
        SelectObject(d.hDC,oldPen);SelectObject(d.hDC,oldBrush);DeleteObject(pen);
    }else{auto s=controlText(d.hwndItem);DrawTextW(d.hDC,s.c_str(),int(s.size()),&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
    if(d.itemState&ODS_FOCUS){InflateRect(&r,-3,-3);DrawFocusRect(d.hDC,&r);}
}
struct ImageRow {std::string path;std::wstring metadata;uint64_t timelineCount=0,runtimeCount=0;bool dependency=false,drawn=false,registered=false;};
struct Application;
LRESULT CALLBACK treeInput(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
LRESULT CALLBACK canvasInput(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
struct CanvasDialog {Application* app=nullptr;HWND window=nullptr,owner=nullptr,width=nullptr,height=nullptr,left=nullptr,top=nullptr,right=nullptr,bottom=nullptr,result=nullptr,origin=nullptr,error=nullptr;bool done=false,filling=false;rep::CanvasSettings draft;std::vector<HWND> multipleControls,sizeControls,paddingControls;bool read(rep::CanvasSettings& settings);void update();void fill();void create();bool apply();};
LRESULT CALLBACK canvasProcedure(HWND,UINT,WPARAM,LPARAM);
struct AudioPopup {Application* app=nullptr;HWND window=nullptr,slider=nullptr,level=nullptr;void show();};
LRESULT CALLBACK audioProcedure(HWND,UINT,WPARAM,LPARAM);
struct ExportDialog {Application* app=nullptr;HWND window=nullptr,format=nullptr,fps=nullptr,alpha=nullptr,audio=nullptr,audioHint=nullptr,directory=nullptr,name=nullptr,description=nullptr,alphaHint=nullptr,sizeInfo=nullptr;bool done=false,accepted=false;rep::ExportOptions options;void update();void create();void submit();};
LRESULT CALLBACK exportProcedure(HWND,UINT,WPARAM,LPARAM);
struct Application {
    HWND window=nullptr,tree=nullptr,canvas=nullptr,choose=nullptr,replayButton=nullptr,stopButton=nullptr,playButton=nullptr,search=nullptr,zhButton=nullptr,enButton=nullptr,exportButton=nullptr;
    HWND pathLabel=nullptr,clientTitle=nullptr,playlistTitle=nullptr,playlistCount=nullptr,playlistFooter=nullptr,imgTitle=nullptr,imgCount=nullptr,imgCurrent=nullptr,imgAll=nullptr,imgShowAll=nullptr,imgCaption=nullptr,imgList=nullptr,imgDetail=nullptr,imgFooter=nullptr,label=nullptr,currentTitle=nullptr,currentJob=nullptr,clockLabel=nullptr;
    HWND treeHost=nullptr,imageHost=nullptr,previousButton=nullptr,nextButton=nullptr,frameLabel=nullptr;
    HWND imgRemoveBackground=nullptr,imgRemoveMonsters=nullptr,imgRemoveCharacters=nullptr,openButton=nullptr,canvasButton=nullptr,refreshButton=nullptr;
    HWND muteButton=nullptr,volumeButton=nullptr;AudioPopup audioPopup;int audioVolume=100;bool audioMuted=false;
    HFONT font=nullptr,smallFont=nullptr,groupFont=nullptr;HBRUSH backgroundBrush=nullptr,surfaceBrush=nullptr,inputBrush=nullptr;HIMAGELIST imageHeight=nullptr;
    NativeScrollbar treeScroll,imageScroll;PaneSplitter verticalSplitter,horizontalSplitter;
    struct GroupRow {std::wstring title;std::string key;int depth=0,count=0;};std::vector<GroupRow> groupRows;
    int sidebarWidth=390,playlistSplit=0;double playlistFraction=.49;
    rep::ClientProtocolSelection protocol;
    rep::CanvasSettings canvasSettings;
    std::filesystem::path root,client,folder,testReport,openPath,badPath,selectedPath;std::unique_ptr<Catalog> catalog;std::vector<size_t> visibleItems;std::map<std::filesystem::path,HTREEITEM> leaves;
    std::unique_ptr<PlaybackController> worker;std::unordered_set<std::string> hiddenImages;std::vector<ImageRow> imageRows;PlayerStatus current,first,second,hot;
    bool english=false,allImg=false,rebuilding=false,updatingImages=false,test=false,featureTest=false,revisionTest=false,inputTest=false,imgFilterTest=false,inputSpace=false,replayed=false,switched=false,recovered=false,benchmark=false;
    int testStage=0,sidebarX=0,footerY=0;Clock::time_point testStart=Clock::now(),stageAt=Clock::now();uint64_t captureSerial=0;uint32_t featureCrc=0;int featureTimestamp=0;std::string featureImg;std::map<std::string,bool> featureResults;
    uint64_t revisionOrdinal=0;int64_t revisionElapsed=0;
    int inputFocus=0;
    bool imgFilterWholeMatches=false,imgFilterOtherFrameMatch=false;uint64_t imgFilterMatchingPaths=0;
    std::thread exportThread;std::mutex exportMutex;std::atomic<bool> exporting=false,cancelExport=false;bool exportDone=false,exportFailed=false;std::wstring exportMessage;
    std::wstring t(const wchar_t* zh,const wchar_t* en)const{return english?en:zh;}
    ~Application(){cancelExport=true;if(exportThread.joinable())exportThread.join();if(imageHeight)ImageList_Destroy(imageHeight);if(font)DeleteObject(font);if(smallFont)DeleteObject(smallFont);if(groupFont)DeleteObject(groupFont);if(backgroundBrush)DeleteObject(backgroundBrush);if(surfaceBrush)DeleteObject(surfaceBrush);if(inputBrush)DeleteObject(inputBrush);}
    HWND child(const wchar_t* cls,const wchar_t* title,DWORD style,int id=0,HWND parent=nullptr){auto w=CreateWindowExW(0,cls,title,WS_CHILD|WS_VISIBLE|style,0,0,1,1,parent?parent:window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(w,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return w;}
    void saveSettings(){if(test)return;std::filesystem::create_directories(root/L"runtime");std::ofstream(root/L"runtime"/L"client.txt")<<rep::utf8(client.wstring());std::ofstream(root/L"runtime"/L"language.txt")<<(english?"en":"zh");std::ofstream(root/L"runtime"/L"layout.txt")<<sidebarWidth<<" "<<playlistFraction;std::ofstream(root/L"runtime"/L"canvas.txt")<<int(canvasSettings.mode)<<" "<<std::setprecision(17)<<canvasSettings.factor<<" "<<canvasSettings.width<<" "<<canvasSettings.height<<" "<<canvasSettings.left<<" "<<canvasSettings.top<<" "<<canvasSettings.right<<" "<<canvasSettings.bottom;std::ofstream(root/L"runtime"/L"audio.txt")<<audioVolume<<" "<<(audioMuted?1:0);}
    void updateAudioControls(){text(muteButton,audioMuted?L"🔇":L"🔊");text(volumeButton,t(L"音量…",L"Vol…"));InvalidateRect(muteButton,nullptr,FALSE);if(audioPopup.level)text(audioPopup.level,std::to_wstring(audioVolume)+L"%");}
    void setAudioVolume(int value){audioVolume=std::clamp(value,0,100);worker->setAudioVolume(audioVolume/100.f);updateAudioControls();saveSettings();}
    void toggleAudioMute(){audioMuted=!audioMuted;worker->setAudioMuted(audioMuted);updateAudioControls();saveSettings();}
    void applyCanvasSettings(const rep::CanvasSettings& settings){canvasSettings=settings;worker->setCanvasSettings(settings);saveSettings();}
    void showCanvas(HWND owner=nullptr){CanvasDialog dialog;dialog.app=this;dialog.owner=owner?owner:window;dialog.draft=canvasSettings;dialog.create();EnableWindow(dialog.owner,FALSE);ShowWindow(dialog.window,SW_SHOW);MSG msg;while(!dialog.done&&GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(dialog.window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}EnableWindow(dialog.owner,TRUE);SetForegroundWindow(dialog.owner);}
    const SkillItem* selectedItem()const{for(const auto& item:catalog->items())if(item.path==selectedPath)return &item;return nullptr;}
    void language(){updateAudioControls();text(pathLabel,client.empty()?t(L"请选择客户端目录",L"Select a client folder"):client.wstring());text(clientTitle,t(L"客户端目录",L"Client folder"));text(choose,t(L"选择客户端",L"Select client"));text(openButton,t(L"打开 REP",L"Open REP"));text(canvasButton,t(L"画布设置…",L"Canvas…"));text(refreshButton,t(L"刷新 NPK",L"Refresh NPK"));text(playlistTitle,t(L"播放列表",L"Playlist"));text(imgTitle,t(L"IMG 资源",L"IMG resources"));text(imgCurrent,t(L"当前画面",L"Current frame"));text(imgAll,t(L"全部 IMG",L"All IMG"));text(imgShowAll,t(L"全部显示",L"Show all"));text(imgRemoveBackground,t(L"移除背景",L"Hide BG"));text(imgRemoveMonsters,t(L"移除怪物",L"Hide monsters"));text(imgRemoveCharacters,t(L"移除人物",L"Hide people"));text(stopButton,t(L"■  停止",L"■  Stop"));text(previousButton,t(L"前帧",L"|◀"));text(nextButton,t(L"后帧",L"▶|"));text(replayButton,t(L"↻  重播",L"↻  Replay"));text(exportButton,t(L"↓  导出素材",L"↓  Export media"));text(imgFooter,t(L"勾选显示 · 取消隐藏 · 预览与导出共用",L"Checked = visible · Shared by preview and export"));SendMessageW(search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(english?L"Search Chinese / English / REP filename":L"搜索中文 / English / REP 文件名"));populate();updateImages(true);updateStatus();InvalidateRect(window,nullptr,FALSE);saveSettings();}
    bool clientReady()const{return !client.empty()&&std::filesystem::is_directory(client/L"Replay")&&std::filesystem::is_directory(client/L"ImagePacks2");}
    std::filesystem::path skillFolder()const{return folder/L"SkillReplay";}
    void configureClient(const std::filesystem::path& path){if(!std::filesystem::is_directory(path/L"Replay")||!std::filesystem::is_directory(path/L"ImagePacks2")){MessageBoxW(window,t(L"请选择包含 Replay 和 ImagePacks2 的客户端目录。",L"Select a folder containing Replay and ImagePacks2.").c_str(),L"REP Player",MB_OK|MB_ICONERROR);return;}client=path;folder=client/L"Replay";selectedPath.clear();hiddenImages.clear();catalog->scan(folder);worker->configureClient(client);text(pathLabel,client.wstring());populate();saveSettings();}
    void chooseClient(){std::filesystem::path path;if(chooseDirectory(window,client,t(L"选择 Windows 客户端目录",L"Select Windows client folder").c_str(),path))configureClient(path);}
    void openSpecified(const std::filesystem::path& path){
        if(!clientReady()){text(label,t(L"请先选择客户端目录，再打开 REP。",L"Select a client folder before opening a REP."));return;}
        if(!std::filesystem::is_regular_file(path)||rep::canonical(rep::utf8(path.extension().wstring()))!=".rep")return;
        selectedPath=path;
        if(selectedItem()){if(!controlText(search).empty())SetWindowTextW(search,L"");else populate();}
        else TreeView_SelectItem(tree,nullptr);
        select(path);
    }
    void chooseReplay(){
        std::filesystem::path path;auto initial=selectedPath.empty()?folder:selectedPath.parent_path();
        if(!chooseRepFile(window,initial,t(L"打开 REP 文件",L"Open REP file").c_str(),path))return;
        if(!clientReady())chooseClient();
        if(clientReady())openSpecified(path);
    }
    void populate(){
        rebuilding=true;SendMessageW(tree,WM_SETREDRAW,FALSE,0);TreeView_DeleteAllItems(tree);leaves.clear();visibleItems.clear();groupRows.clear();
        auto query=rep::utf8(controlText(search));std::map<std::string,HTREEITEM> groups;std::set<HTREEITEM> expandAfterInsert;const auto& items=catalog->items();
        std::map<std::string,std::wstring> jobTitles;
        if(!english)for(const auto& item:items)if(item.directories.size()>1&&rep::canonical(item.directories[0])=="skillreplay"&&!item.jobZh.empty())jobTitles[rep::canonical(item.directories[1])]=rep::wide(item.jobZh);
        for(size_t i=0;i<items.size();i++){
            const auto& item=items[i];if(!Catalog::matches(item,query))continue;visibleItems.push_back(i);
            HTREEITEM parent=TVI_ROOT;std::filesystem::path accumulated;int depth=0;
            bool skillBranch=!item.directories.empty()&&rep::canonical(item.directories[0])=="skillreplay";
            for(const auto& directory:item.directories){
                auto part=std::filesystem::path(rep::wide(directory));
                accumulated/=part;auto key=rep::utf8(accumulated.generic_wstring());auto found=groups.find(key);
                if(found==groups.end()){
                    GroupRow row;row.title=part.wstring();if(skillBranch&&depth==1){auto title=jobTitles.find(rep::canonical(directory));if(title!=jobTitles.end())row.title=title->second;}row.key=key;row.depth=depth;groupRows.push_back(row);
                    TVINSERTSTRUCTW add{};add.hParent=parent;add.hInsertAfter=TVI_LAST;add.item.mask=TVIF_TEXT|TVIF_PARAM;add.item.pszText=groupRows.back().title.data();add.item.lParam=-LPARAM(groupRows.size());parent=TreeView_InsertItem(tree,&add);groups[key]=parent;
                }else parent=found->second;
                TVITEMW group{};group.mask=TVIF_PARAM;group.hItem=parent;TreeView_GetItem(tree,&group);groupRows[size_t(-group.lParam-1)].count++;
                if(!query.empty()||item.path==selectedPath||(selectedPath.empty()&&skillBranch&&item.job=="Swordman"))expandAfterInsert.insert(parent);
                depth++;
            }
            auto title=Catalog::display(item,english);TVINSERTSTRUCTW add{};add.hParent=parent;add.hInsertAfter=TVI_LAST;add.item.mask=TVIF_TEXT|TVIF_PARAM;add.item.pszText=title.data();add.item.lParam=LPARAM(i+1);leaves[item.path]=TreeView_InsertItem(tree,&add);
        }
        for(auto group:expandAfterInsert)TreeView_Expand(tree,group,TVE_EXPAND);
        if(auto it=leaves.find(selectedPath);it!=leaves.end()){TreeView_SelectItem(tree,it->second);TreeView_EnsureVisible(tree,it->second);}
        SendMessageW(tree,WM_SETREDRAW,TRUE,0);InvalidateRect(tree,nullptr,TRUE);rebuilding=false;treeScroll.refresh();
        text(playlistCount,std::to_wstring(visibleItems.size())+t(L" 个 REP",L" REP files"));text(playlistFooter,visibleItems.empty()?t(L"没有匹配的 REP",L"No matching REP"):t(L"中文与英文均可搜",L"Chinese and English search"));
    }
    LRESULT drawTree(NMTVCUSTOMDRAW& draw){
        if(draw.nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
        if(draw.nmcd.dwDrawStage!=CDDS_ITEMPREPAINT)return CDRF_DODEFAULT;
        auto dc=draw.nmcd.hdc;RECT row=draw.nmcd.rc,visible;GetClientRect(treeHost,&visible);row.left=0;row.right=visible.right;
        SetBkMode(dc,TRANSPARENT);auto oldPen=SelectObject(dc,GetStockObject(NULL_PEN));auto oldBrush=SelectObject(dc,GetStockObject(DC_BRUSH));
        if(draw.nmcd.lItemlParam<0){
            auto index=size_t(-draw.nmcd.lItemlParam-1);if(index>=groupRows.size())return CDRF_DODEFAULT;const auto& group=groupRows[index];
            SetDCBrushColor(dc,RGB(48,50,54));FillRect(dc,&row,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));RECT divider=row;divider.bottom=divider.top+1;SetDCBrushColor(dc,RGB(64,67,73));FillRect(dc,&divider,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            int x=14+group.depth*18,y=(row.top+row.bottom)/2;bool expanded=(TreeView_GetItemState(tree,reinterpret_cast<HTREEITEM>(draw.nmcd.dwItemSpec),TVIS_EXPANDED)&TVIS_EXPANDED)!=0;POINT arrow[3];if(expanded){arrow[0]={x-4,y-2};arrow[1]={x+4,y-2};arrow[2]={x,y+3};}else{arrow[0]={x-2,y-4};arrow[1]={x+3,y};arrow[2]={x-2,y+4};}SetDCBrushColor(dc,RGB(159,164,174));Polygon(dc,arrow,3);
            RECT title=row;title.left=x+14;title.right-=43;SelectObject(dc,groupFont);SetTextColor(dc,RGB(204,208,216));DrawTextW(dc,group.title.c_str(),int(group.title.size()),&title,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
            RECT count=row;count.left=count.right-40;count.right-=7;SelectObject(dc,smallFont);SetTextColor(dc,Muted);auto amount=std::to_wstring(group.count);DrawTextW(dc,amount.c_str(),int(amount.size()),&count,DT_SINGLELINE|DT_VCENTER|DT_RIGHT);
        }else if(draw.nmcd.lItemlParam>0){
            size_t index=draw.nmcd.lItemlParam-1;if(index>=catalog->items().size())return CDRF_DODEFAULT;const auto& item=catalog->items()[index];bool selected=item.path==selectedPath;
            SetDCBrushColor(dc,selected?Selected:Surface);FillRect(dc,&row,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));if(selected){RECT accent=row;accent.right=3;SetDCBrushColor(dc,Yellow);FillRect(dc,&accent,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));}
            int depth=0;for(auto parent=TreeView_GetParent(tree,reinterpret_cast<HTREEITEM>(draw.nmcd.dwItemSpec));parent;parent=TreeView_GetParent(tree,parent))depth++;
            int x=13+depth*18;POINT mark[3]{{x,row.top+15},{x+5,row.top+19},{x,row.top+23}};SetDCBrushColor(dc,selected?Yellow:RGB(106,115,129));Polygon(dc,mark,3);
            RECT line=row;line.left=x+13;line.right-=9;line.top+=2;line.bottom=line.top+20;SelectObject(dc,font);SetTextColor(dc,selected?RGB(241,232,184):Text);auto title=Catalog::display(item,english);DrawTextW(dc,title.c_str(),int(title.size()),&line,DT_SINGLELINE|DT_END_ELLIPSIS);
            line.top+=20;line.bottom=line.top+16;SelectObject(dc,smallFont);SetTextColor(dc,selected?RGB(174,171,153):Muted);auto alias=english?(item.zh.empty()?rep::wide(item.relativePath):rep::wide(item.zh)):item.path.filename().wstring();DrawTextW(dc,alias.c_str(),int(alias.size()),&line,DT_SINGLELINE|DT_END_ELLIPSIS);
        }
        SelectObject(dc,oldBrush);SelectObject(dc,oldPen);return CDRF_SKIPDEFAULT;
    }
    void open(const std::filesystem::path& path){if(!clientReady())return;selectedPath=path;hiddenImages.clear();updatingImages=true;ListView_DeleteAllItems(imgList);updatingImages=false;imageRows.clear();worker->open(path);current=worker->status();updateImages(true);updateStatus();}
    void select(const std::filesystem::path& path){auto it=leaves.find(path);if(it!=leaves.end()){if(TreeView_GetSelection(tree)==it->second)open(path);else TreeView_SelectItem(tree,it->second);TreeView_EnsureVisible(tree,it->second);}else open(path);}
    void layout(){
        RECT r;GetClientRect(window,&r);int w=r.right,h=r.bottom;if(w<=0||h<=0)return;
        sidebarWidth=std::clamp(sidebarWidth,300,std::max(300,w-610));int side=sidebarWidth;sidebarX=w-side;footerY=h-109;
        playlistSplit=std::clamp(44+int((h-44)*playlistFraction),274,std::max(274,h-ImagePanelMinimum));int split=playlistSplit,left=sidebarX-6;
        MoveWindow(clientTitle,13,11,84,24,TRUE);MoveWindow(pathLabel,100,11,std::max(1,w-708),24,TRUE);MoveWindow(refreshButton,w-600,7,112,29,TRUE);MoveWindow(canvasButton,w-479,7,96,29,TRUE);MoveWindow(openButton,w-374,7,106,29,TRUE);MoveWindow(choose,w-259,7,115,29,TRUE);MoveWindow(zhButton,w-135,7,63,29,TRUE);MoveWindow(enButton,w-68,7,55,29,TRUE);
        MoveWindow(canvas,0,44,std::max(1,left),std::max(1,footerY-44),TRUE);MoveWindow(currentTitle,15,footerY+9,std::max(1,left-332),22,TRUE);MoveWindow(frameLabel,left-306,footerY+10,150,21,TRUE);MoveWindow(currentJob,left-149,footerY+10,46,21,TRUE);MoveWindow(muteButton,left-96,footerY+5,35,29,TRUE);MoveWindow(volumeButton,left-55,footerY+5,41,29,TRUE);
        MoveWindow(playButton,13,footerY+50,42,39,TRUE);MoveWindow(stopButton,62,footerY+50,61,39,TRUE);MoveWindow(replayButton,129,footerY+50,63,39,TRUE);MoveWindow(previousButton,199,footerY+50,35,39,TRUE);MoveWindow(nextButton,240,footerY+50,35,39,TRUE);MoveWindow(clockLabel,288,footerY+58,std::max(1,left-422),24,TRUE);MoveWindow(exportButton,left-125,footerY+52,111,35,TRUE);MoveWindow(label,15,h-18,std::max(1,left-25),18,TRUE);
        MoveWindow(playlistTitle,sidebarX+13,55,130,24,TRUE);MoveWindow(playlistCount,w-150,57,137,20,TRUE);MoveWindow(search,sidebarX+12,89,side-24,30,TRUE);
        int contentWidth=side-31,treeHeight=std::max(1,split-159),imageHeightPixels=std::max(1,h-split-229),nativeBar=GetSystemMetrics(SM_CXVSCROLL);
        MoveWindow(treeHost,sidebarX+8,128,contentWidth,treeHeight,TRUE);MoveWindow(tree,0,0,contentWidth+nativeBar,treeHeight,TRUE);MoveWindow(treeScroll.window,w-17,128,8,treeHeight,TRUE);MoveWindow(playlistFooter,sidebarX+13,split-25,side-26,22,TRUE);
        MoveWindow(imgTitle,sidebarX+13,split+14,145,22,TRUE);MoveWindow(imgCount,w-135,split+15,122,20,TRUE);MoveWindow(imgCurrent,sidebarX+3,split+43,side/2-3,33,TRUE);MoveWindow(imgAll,sidebarX+side/2,split+43,side/2-3,33,TRUE);MoveWindow(imgCaption,sidebarX+12,split+123,side-110,20,TRUE);MoveWindow(imgShowAll,w-93,split+117,80,27,TRUE);
        int bulkWidth=(side-36)/3;MoveWindow(imgRemoveBackground,sidebarX+12,split+83,bulkWidth,28,TRUE);MoveWindow(imgRemoveMonsters,sidebarX+18+bulkWidth,split+83,bulkWidth,28,TRUE);MoveWindow(imgRemoveCharacters,sidebarX+24+bulkWidth*2,split+83,bulkWidth,28,TRUE);
        MoveWindow(imageHost,sidebarX+8,split+149,contentWidth,imageHeightPixels,TRUE);MoveWindow(imgList,0,0,contentWidth+nativeBar,imageHeightPixels,TRUE);MoveWindow(imageScroll.window,w-17,split+149,8,imageHeightPixels,TRUE);MoveWindow(imgDetail,sidebarX+12,h-72,side-24,42,TRUE);MoveWindow(imgFooter,sidebarX+12,h-25,side-24,20,TRUE);
        ListView_SetColumnWidth(imgList,0,std::max(140,contentWidth-112));ListView_SetColumnWidth(imgList,1,108);
        MoveWindow(verticalSplitter.window,sidebarX-6,44,6,h-44,TRUE);MoveWindow(horizontalSplitter.window,sidebarX,split,side,6,TRUE);
        treeScroll.refresh();imageScroll.refresh();InvalidateRect(window,nullptr,TRUE);
    }
    HBRUSH staticBackground(HWND control,HDC dc)const{
        bool sidebar=control==playlistTitle||control==playlistCount||control==playlistFooter||control==imgTitle||control==imgCount||control==imgCaption||control==imgDetail||control==imgFooter;
        SetBkColor(dc,sidebar?Surface:Background);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,control==label?Muted:Text);return sidebar?surfaceBrush:backgroundBrush;
    }
    void updateImages(bool force=false){std::map<std::string,std::vector<rep::ImgCall>> groups;for(const auto& call:allImg?current.allImages:current.currentImages)groups[call.path].push_back(call);std::vector<ImageRow> rows;
        for(const auto& [path,calls]:groups){ImageRow row;row.path=path;std::set<int> frames;uint64_t count=0;std::set<uint32_t> layers;for(const auto& call:calls){if(call.frame>=0)frames.insert(call.frame);count+=call.count;row.timelineCount+=call.timelineCount;row.runtimeCount+=call.runtimeCount;layers.insert(call.layer);row.dependency|=call.dependency;row.drawn|=call.drawn;row.registered|=call.registered;}
            if(allImg&&!row.drawn)row.metadata=count||row.timelineCount||row.runtimeCount?t(L"调用未绘制",L"Called"):t(L"仅注册",L"Registered");else {for(auto f:frames){if(!row.metadata.empty())row.metadata+=L"/";row.metadata+=std::to_wstring(f);}if(count)row.metadata+=L" ×"+std::to_wstring(count);}if(row.dependency)row.metadata+=t(L" · 输入",L" · Input");else if(!allImg&&!layers.empty())row.metadata+=L" L"+std::to_wstring(*layers.begin());if(hiddenImages.contains(path))row.metadata+=t(L" · 已隐藏",L" · Hidden");rows.push_back(std::move(row));}
        int top=ListView_GetTopIndex(imgList),scrollX=GetScrollPos(imgList,SB_HORZ),focus=ListView_GetNextItem(imgList,-1,LVNI_FOCUSED),sel=ListView_GetNextItem(imgList,-1,LVNI_SELECTED);RECT topRect{};ListView_GetItemRect(imgList,top,&topRect,LVIR_BOUNDS);std::string topPath=top>=0&&size_t(top)<imageRows.size()?imageRows[top].path:"",focusPath=focus>=0&&size_t(focus)<imageRows.size()?imageRows[focus].path:"",selPath=sel>=0&&size_t(sel)<imageRows.size()?imageRows[sel].path:"";bool changed=rows.size()!=imageRows.size();if(!changed)for(size_t i=0;i<rows.size();i++)if(rows[i].path!=imageRows[i].path){changed=true;break;}updatingImages=true;SendMessageW(imgList,WM_SETREDRAW,FALSE,0);
        if(changed){ListView_DeleteAllItems(imgList);for(size_t i=0;i<rows.size();i++){auto path=rep::wide(rows[i].path);LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=int(i);item.pszText=path.data();ListView_InsertItem(imgList,&item);}}
        bool metadataChanged=false;for(size_t i=0;i<rows.size();i++){if(force||changed||rows[i].metadata!=imageRows[i].metadata){ListView_SetItemText(imgList,int(i),1,rows[i].metadata.data());metadataChanged=true;}bool checked=!hiddenImages.contains(rows[i].path);if(bool(ListView_GetCheckState(imgList,int(i)))!=checked){ListView_SetCheckState(imgList,int(i),checked);metadataChanged=true;}if(changed&&rows[i].path==selPath)ListView_SetItemState(imgList,int(i),LVIS_SELECTED,LVIS_SELECTED);if(changed&&rows[i].path==focusPath)ListView_SetItemState(imgList,int(i),LVIS_FOCUSED,LVIS_FOCUSED);}
        imageRows=std::move(rows);if(changed&&!imageRows.empty()){int anchor=std::min(top,int(imageRows.size()-1));for(size_t i=0;i<imageRows.size();i++)if(imageRows[i].path==topPath){anchor=int(i);break;}ListView_EnsureVisible(imgList,anchor,FALSE);RECT newTop{};ListView_GetItemRect(imgList,anchor,&newTop,LVIR_BOUNDS);ListView_Scroll(imgList,scrollX-GetScrollPos(imgList,SB_HORZ),newTop.top-topRect.top);}SendMessageW(imgList,WM_SETREDRAW,TRUE,0);updatingImages=false;if(changed||force||metadataChanged)InvalidateRect(imgList,nullptr,TRUE);text(imgCount,t(L"已隐藏 ",L"Hidden ")+std::to_wstring(hiddenImages.size()));text(imgCaption,std::to_wstring(imageRows.size())+t(allImg?L" 路径 · 整段回放":L" 路径 · 当前调用",allImg?L" paths · Full replay":L" paths · Current calls"));int chosen=ListView_GetNextItem(imgList,-1,LVNI_SELECTED);text(imgDetail,chosen>=0&&size_t(chosen)<imageRows.size()?rep::wide(imageRows[chosen].path):t(L"选择 IMG 查看完整逻辑路径",L"Select IMG to inspect the full path"));
    }
    void imageChanged(const NMLISTVIEW& e){if(updatingImages||e.iItem<0||size_t(e.iItem)>=imageRows.size())return;if((e.uChanged&LVIF_STATE)&&((e.uNewState^e.uOldState)&LVIS_STATEIMAGEMASK)){auto path=imageRows[e.iItem].path;if(ListView_GetCheckState(imgList,e.iItem))hiddenImages.erase(path);else hiddenImages.insert(path);worker->setHiddenImages(hiddenImages);updateImages(true);}if(e.uChanged&LVIF_STATE){int i=ListView_GetNextItem(imgList,-1,LVNI_SELECTED);if(i>=0&&size_t(i)<imageRows.size())text(imgDetail,rep::wide(imageRows[i].path));}}
    void hideImageCategory(ImageCategory category){
        auto status=worker->status();if(status.path!=selectedPath.wstring()||status.phase==Phase::Empty||status.phase==Phase::Loading||status.phase==Phase::Error)return;
        bool changed=false;auto add=[&](const std::vector<rep::ImgCall>& calls){for(const auto& call:calls)if(matchesImageCategory(call.path,category))changed|=hiddenImages.insert(call.path).second;};
        add(status.allImages);add(status.currentImages);if(!changed)return;
        worker->setHiddenImages(hiddenImages);current=worker->status();updateImages(true);
    }
    void updateStatus(){auto item=selectedItem();auto name=item?Catalog::display(*item,english):selectedPath.filename().wstring();text(currentTitle,name.empty()?t(L"选择右侧 REP 开始播放",L"Select a REP in the playlist"):L"▶  "+name);text(currentJob,item?rep::wide(english?item->job:item->jobZh):L"");std::wstring status;switch(current.phase){case Phase::Loading:status=t(L"正在加载…",L"Loading…");break;case Phase::Playing:status=t(L"正在播放",L"Playing");break;case Phase::Paused:status=t(L"已暂停",L"Paused");break;case Phase::Ended:status=t(L"播放结束，保留末帧",L"Ended · Last frame retained");break;case Phase::Stopped:status=t(L"已停止",L"Stopped");break;case Phase::Error:status=current.message;break;default:status=t(L"GPU 实时渲染",L"GPU rendering");}
        {std::lock_guard lock(exportMutex);if(exporting)status=exportMessage;else if(exportDone)status=(exportFailed?t(L"导出失败：",L"Export failed: "):t(L"已导出：",L"Exported: "))+exportMessage;}if(current.compatibilityIgnored&&current.phase!=Phase::Error&&current.phase!=Phase::Loading)status+=t(L" · 兼容播放：已跳过未识别指令",L" · Compatibility playback: unsupported instruction ignored");if(current.missingSoundCount)status+=t(L" · 缺少声音：",L" · Missing sounds: ")+std::to_wstring(current.missingSoundCount);if(!current.audioMessage.empty())status+=t(L" · 声音：",L" · Audio: ")+current.audioMessage;text(label,status);text(playButton,current.phase==Phase::Playing?L"Ⅱ":L"▶");text(clockLabel,timeText(current.elapsed)+L" / "+(current.duration?timeText(current.duration):L"--:--.---"));text(frameLabel,t(L"帧 ",L"Frame ")+(current.frameCount?std::to_wstring(current.ordinal+1)+L" / "+std::to_wstring(current.frameCount):L"— / —"));bool canStep=!selectedPath.empty()&&current.phase!=Phase::Loading&&current.phase!=Phase::Empty&&current.phase!=Phase::Error;EnableWindow(previousButton,canStep);EnableWindow(nextButton,canStep);for(auto button:{imgRemoveBackground,imgRemoveMonsters,imgRemoveCharacters})EnableWindow(button,canStep);bool imagesReady=worker&&bool(worker->imageIndex());EnableWindow(refreshButton,clientReady()&&current.phase!=Phase::Loading&&!exporting);EnableWindow(exportButton,!selectedPath.empty()&&imagesReady&&current.phase!=Phase::Loading&&current.phase!=Phase::Error&&!exporting);text(window,L"REP Player"+(name.empty()?L"":L" · "+name));InvalidateRect(window,nullptr,FALSE);
    }
    void refreshNpk(){
        if(!clientReady()||exporting)return;
        {std::lock_guard lock(exportMutex);exportDone=false;exportFailed=false;}
        worker->refreshImages();current=worker->status();updateStatus();updateImages(true);
        // Keep commands disabled until the next status update observes completion.
        EnableWindow(refreshButton,FALSE);EnableWindow(exportButton,FALSE);
    }
    void startExport(const rep::ExportOptions& options){
        if(exporting)return;
        auto index=worker->imageIndex();
        if(!index){
            {std::lock_guard lock(exportMutex);exportDone=true;exportFailed=true;exportMessage=t(L"图片资源尚未就绪，请等待加载或刷新完成。",L"Wait for image loading or refresh to finish.");}
            updateStatus();return;
        }
        if(exportThread.joinable())exportThread.join();auto selected=selectedPath,selectedClient=client;cancelExport=false;
        {std::lock_guard lock(exportMutex);exporting=true;exportDone=false;exportFailed=false;exportMessage=t(L"正在准备导出…",L"Preparing export…");}
        bool en=english;auto requested=options;requested.protocol=protocol;requested.canvas=canvasSettings;
        exportThread=std::thread([this,options=std::move(requested),selected,selectedClient,en,index=std::move(index)]{
            try{
                rep::Gpu gpu(root/L"assets"/L"shaders");gpu.createAllPrograms();rep::Assets assets(index);
                auto result=rep::exportReplay(gpu,assets,selectedClient,root/L"runtime"/L"cache"/L"export",selected,options,[this,en](const rep::ExportProgress& p){std::lock_guard lock(exportMutex);exportMessage=(en?L"Exporting ":L"正在导出 ")+std::to_wstring(p.completedFrames)+L" / "+std::to_wstring(p.totalFrames)+L" · "+p.stage;},[this]{return cancelExport.load();});
                std::lock_guard lock(exportMutex);exportMessage=result.outputPath.wstring();if(result.missingSoundCount)exportMessage+=(en?L" · Missing sounds: ":L" · 缺少声音：")+std::to_wstring(result.missingSoundCount);exporting=false;exportDone=true;
            }catch(const std::exception& e){std::lock_guard lock(exportMutex);exportMessage=rep::wide(e.what());exporting=false;exportDone=true;exportFailed=true;}
        });
    }
    void showExport(){ExportDialog dialog;dialog.app=this;dialog.options.outputDirectory=root/L"exports";dialog.options.fileName=selectedPath.stem().wstring();dialog.options.hiddenImages=hiddenImages;dialog.create();EnableWindow(window,FALSE);ShowWindow(dialog.window,SW_SHOW);MSG msg;while(!dialog.done&&GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(dialog.window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}EnableWindow(window,TRUE);SetForegroundWindow(window);if(dialog.accepted)startExport(dialog.options);}
    void finishTest(bool okay){std::filesystem::create_directories(testReport.parent_path());std::ofstream out(testReport);if(featureTest||revisionTest||inputTest||imgFilterTest){out<<"{";for(const auto& [key,value]:featureResults)out<<'"'<<key<<"\":"<<(value?"true":"false")<<',';if(imgFilterTest)out<<"\"all_registered_matches_hidden\":"<<(imgFilterWholeMatches?"true":"false")<<",\"other_frame_match_found\":"<<(imgFilterOtherFrameMatch?"true":"false")<<",\"matching_path_count\":"<<imgFilterMatchingPaths<<',';out<<"\"pass\":"<<(okay?"true":"false")<<"}";}else out<<"{\"tree_replays\":"<<catalog->items().size()<<",\"autoplay\":"<<(first.phase==Phase::Ended?"true":"false")<<",\"switched\":"<<(switched?"true":"false")<<",\"replayed\":"<<(replayed?"true":"false")<<",\"recovered_after_scene_error\":"<<(recovered?"true":"false")<<",\"frozen\":"<<(first.frozen&&hot.frozen&&second.frozen?"true":"false")<<",\"process_first_present_seconds\":"<<first.processReadySeconds<<",\"first_open_seconds\":"<<first.readySeconds<<",\"hot_replay_seconds\":"<<hot.readySeconds<<",\"second_open_seconds\":"<<second.readySeconds<<",\"first_rendered_frames\":"<<first.frames<<",\"second_rendered_frames\":"<<second.frames<<",\"second_skipped_scenes\":"<<second.skipped<<",\"max_frame_ms\":"<<second.maxFrameMilliseconds<<",\"first_p50_frame_ms\":"<<first.p50Frame<<",\"first_p95_frame_ms\":"<<first.p95Frame<<",\"first_p99_frame_ms\":"<<first.p99Frame<<",\"first_max_frame_ms\":"<<first.maxFrameMilliseconds<<",\"first_skipped_scenes\":"<<first.skipped<<",\"first_playback_seconds\":"<<first.playbackSeconds<<",\"hot_playback_seconds\":"<<hot.playbackSeconds<<",\"second_playback_seconds\":"<<second.playbackSeconds<<",\"first_recorded_seconds\":"<<first.timestamp/1000.<<",\"second_recorded_seconds\":"<<second.timestamp/1000.<<",\"pass\":"<<(okay?"true":"false")<<"}";out.close();PostMessageW(window,WM_CLOSE,okay?0:1,0);}
    bool searchContains(const wchar_t* query,const std::filesystem::path& expected){SetWindowTextW(search,query);return leaves.contains(expected);}
    bool clickInputLeaf(const std::filesystem::path& path,bool rowRight){
        auto found=leaves.find(path);if(found==leaves.end())return false;
        TreeView_EnsureVisible(tree,found->second);RECT row{},labelRect{},clip{};
        if(!TreeView_GetItemRect(tree,found->second,&row,FALSE)||!TreeView_GetItemRect(tree,found->second,&labelRect,TRUE))return false;
        GetClientRect(treeHost,&clip);int x=rowRight?int(clip.right)-8:std::min(int(clip.right)-8,int(labelRect.left)+25),y=rowRight?(row.top+row.bottom)/2:row.bottom-5;
        if(x<0||y<0||y>=clip.bottom)return false;
        SetFocus(tree);if(GetFocus()!=tree)return false;
        SendMessageW(tree,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessageW(tree,WM_LBUTTONUP,0,MAKELPARAM(x,y));return true;
    }
    void queueInputSpace(HWND control,bool repeat=false){
        // Queue only key events: the normal application loop must decide whether
        // to translate Space into WM_CHAR or handle the playback shortcut.
        LPARAM down=1|(0x39<<16);if(repeat)down|=LPARAM(1)<<30;
        PostMessageW(control,WM_KEYDOWN,VK_SPACE,down);PostMessageW(control,WM_KEYUP,VK_SPACE,1|(0x39<<16)|(LPARAM(1)<<30)|(LPARAM(1)<<31));
    }
    bool focusInputControl(){HWND control=inputFocus==0?tree:inputFocus==1?imgList:stopButton;SetFocus(control);return GetFocus()==control;}
    void nextInputStage(int stage){testStage=stage;stageAt=Clock::now();}
    bool inputPlaying(const std::filesystem::path& path)const{return current.path==path.wstring()&&current.phase==Phase::Playing&&current.elapsed>=120;}
    void runInputClick(){
        auto divine=skillFolder()/L"Priest"/L"DivinePunishment.rep",bloody=skillFolder()/L"Swordman"/L"BloodyRave.rep",phantom=skillFolder()/L"Swordman"/L"BladePhantomEx.rep";
        if(testStage==0){for(auto key:{"click_alias_switches","click_row_right_switches","click_rapid_latest_plays","click_selected_restarts"})featureResults[key]=false;select(divine);nextInputStage(1);}
        else if(testStage==1&&inputPlaying(divine)){if(!clickInputLeaf(bloody,false)){finishTest(false);return;}nextInputStage(2);}
        else if(testStage==2&&inputPlaying(bloody)){featureResults["click_alias_switches"]=selectedPath==bloody;if(!clickInputLeaf(phantom,true)){finishTest(false);return;}nextInputStage(3);}
        else if(testStage==3&&inputPlaying(phantom)){featureResults["click_row_right_switches"]=selectedPath==phantom;if(!clickInputLeaf(bloody,false)||!clickInputLeaf(divine,true)){finishTest(false);return;}nextInputStage(4);}
        else if(testStage==4&&inputPlaying(divine)){featureResults["click_rapid_latest_plays"]=selectedPath==divine;SendMessageW(window,WM_COMMAND,PlayPause,0);nextInputStage(5);}
        else if(testStage==5&&current.phase==Phase::Paused){revisionElapsed=current.elapsed;if(!clickInputLeaf(divine,true)){finishTest(false);return;}nextInputStage(6);}
        else if(testStage==6&&inputPlaying(divine)){featureResults["click_selected_restarts"]=selectedPath==divine;finishTest(std::all_of(featureResults.begin(),featureResults.end(),[](const auto& result){return result.second;}));nextInputStage(7);}
    }
    void runInputSpace(){
        auto bloody=skillFolder()/L"Swordman"/L"BloodyRave.rep";
        if(testStage==0){for(auto key:{"space_tree_pause_resume","space_img_pause_resume","space_toolbar_pause_resume","space_held_key_single_toggle","space_hidden_retained","space_search_types_literal"})featureResults[key]=false;select(bloody);nextInputStage(1);}
        else if(testStage==1&&inputPlaying(bloody)&&current.elapsed>=350){
            int chosen=-1;for(size_t i=0;i<imageRows.size();i++)if(imageRows[i].drawn&&!imageRows[i].dependency){chosen=int(i);featureImg=imageRows[i].path;break;}
            if(chosen<0||!focusInputControl()){finishTest(false);return;}ListView_SetCheckState(imgList,chosen,FALSE);featureResults["space_hidden_retained"]=true;queueInputSpace(GetFocus());nextInputStage(2);
        }
        else if(testStage==2&&current.phase==Phase::Paused){revisionElapsed=current.elapsed;featureResults["space_hidden_retained"]&=current.hiddenImages==hiddenImages&&current.hiddenImages.contains(featureImg);if(inputFocus==0)queueInputSpace(GetFocus(),true);nextInputStage(3);}
        else if(testStage==3&&Clock::now()-stageAt>std::chrono::milliseconds(250)){
            bool frozen=current.phase==Phase::Paused&&current.elapsed==revisionElapsed&&current.path==bloody.wstring();if(inputFocus==0)featureResults["space_held_key_single_toggle"]=frozen;if(!frozen){finishTest(false);return;}
            queueInputSpace(GetFocus());nextInputStage(4);
        }
        else if(testStage==4&&inputPlaying(bloody)&&current.elapsed>revisionElapsed+120){
            const char* keys[]{"space_tree_pause_resume","space_img_pause_resume","space_toolbar_pause_resume"};featureResults[keys[inputFocus]]=true;featureResults["space_hidden_retained"]&=current.hiddenImages==hiddenImages&&current.hiddenImages.contains(featureImg);
            if(++inputFocus<3){if(!focusInputControl()){finishTest(false);return;}queueInputSpace(GetFocus());nextInputStage(2);}
            else {SetWindowTextW(search,L"BloodyRave");SetFocus(search);if(GetFocus()!=search){finishTest(false);return;}SendMessageW(search,EM_SETSEL,10,10);revisionElapsed=current.elapsed;queueInputSpace(search);nextInputStage(5);}
        }
        else if(testStage==5&&Clock::now()-stageAt>std::chrono::milliseconds(250)){
            featureResults["space_search_types_literal"]=controlText(search)==L"BloodyRave "&&current.phase==Phase::Playing&&current.elapsed>revisionElapsed;featureResults["space_hidden_retained"]&=current.hiddenImages==hiddenImages&&current.hiddenImages.contains(featureImg);
            finishTest(std::all_of(featureResults.begin(),featureResults.end(),[](const auto& result){return result.second;}));nextInputStage(6);
        }
    }
    void runInput(){
        if(current.phase==Phase::Error||Clock::now()-testStart>std::chrono::seconds(25)||(testStage>0&&Clock::now()-stageAt>std::chrono::seconds(4))){finishTest(false);return;}
        if(inputSpace)runInputSpace();else runInputClick();
    }
    void dragControl(HWND control,int dx,int dy){
        RECT r;GetClientRect(control,&r);int x=(r.right-r.left)/2,y=(r.bottom-r.top)/2;
        SendMessageW(control,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessageW(control,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x+dx,y+dy));SendMessageW(control,WM_LBUTTONUP,0,MAKELPARAM(x+dx,y+dy));
    }
    bool checkStaticBackgrounds(){
        bool okay=true;
        for(auto control:{clientTitle,pathLabel,playlistTitle,playlistCount,playlistFooter,imgTitle,imgCount,imgCaption,imgDetail,imgFooter,currentTitle,currentJob,clockLabel,frameLabel,label}){
            auto dc=GetDC(control);auto brush=reinterpret_cast<HBRUSH>(SendMessageW(window,WM_CTLCOLORSTATIC,reinterpret_cast<WPARAM>(dc),reinterpret_cast<LPARAM>(control)));LOGBRUSH measured{};bool sidebar=control==playlistTitle||control==playlistCount||control==playlistFooter||control==imgTitle||control==imgCount||control==imgCaption||control==imgDetail||control==imgFooter;
            COLORREF expected=sidebar?Surface:Background;okay&=GetObjectW(brush,sizeof(measured),&measured)==sizeof(measured)&&measured.lbColor==expected&&GetBkColor(dc)==expected&&GetBkMode(dc)==TRANSPARENT;ReleaseDC(control,dc);
        }
        return okay;
    }
    bool checkScrollbars(){
        bool okay=true;
        auto exercise=[&](NativeScrollbar& bar,HWND host){
            RECT nativeRect,clipRect;GetWindowRect(bar.target,&nativeRect);GetWindowRect(host,&clipRect);SCROLLBARINFO nativeInfo{};nativeInfo.cbSize=sizeof(nativeInfo);GetScrollBarInfo(bar.target,OBJID_VSCROLL,&nativeInfo);RECT overlap{};bool clipped=!IntersectRect(&overlap,&nativeInfo.rcScrollBar,&clipRect);bool noHorizontal=(GetWindowLongPtrW(bar.target,GWL_STYLE)&WS_HSCROLL)==0;
            bar.scrollTo(0);auto before=bar.range();RECT thumb=bar.thumbRect(),track;GetClientRect(bar.window,&track);int x=std::max(1,int(track.right)/2),y=(thumb.top+thumb.bottom)/2;
            SendMessageW(bar.window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessageW(bar.window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x,track.bottom));SendMessageW(bar.window,WM_LBUTTONUP,0,MAKELPARAM(x,track.bottom));auto after=bar.range();
            RECT last{};if(bar.tree){auto item=TreeView_GetRoot(bar.target);for(auto next=TreeView_GetNextVisible(bar.target,item);next;next=TreeView_GetNextVisible(bar.target,item))item=next;TreeView_GetItemRect(bar.target,item,&last,FALSE);}else ListView_GetItemRect(bar.target,ListView_GetItemCount(bar.target)-1,&last,LVIR_BOUNDS);
            RECT hostClient;GetClientRect(host,&hostClient);bool reachesLast=last.bottom<=hostClient.bottom&&last.top>=0;
            SendMessageW(bar.window,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0);auto wheel=bar.range();bool moved=before.maximum>0&&after.position>before.position&&wheel.position<after.position;
            return clipped&&nativeRect.right>clipRect.right&&noHorizontal&&moved&&reachesLast&&IsWindowVisible(bar.window);
        };
        okay&=exercise(treeScroll,treeHost);SendMessageW(window,WM_COMMAND,AllImages,0);okay&=exercise(imageScroll,imageHost);SendMessageW(window,WM_COMMAND,CurrentImages,0);return okay;
    }
    void checkRevisionLayout(){
        RECT beforeCanvas,beforeTree,afterCanvas,afterTree;GetWindowRect(canvas,&beforeCanvas);GetWindowRect(treeHost,&beforeTree);
        int savedWidth=sidebarWidth;double savedFraction=playlistFraction;
        dragControl(verticalSplitter.window,-70,0);GetWindowRect(canvas,&afterCanvas);featureResults["splitter_vertical_resize"]=afterCanvas.right-afterCanvas.left<beforeCanvas.right-beforeCanvas.left&&sidebarWidth>savedWidth;
        dragControl(verticalSplitter.window,5000,0);featureResults["splitter_vertical_resize"]&=sidebarWidth==300;sidebarWidth=savedWidth;layout();
        int restoredWidth=sidebarWidth;ShowWindow(window,SW_MINIMIZE);ShowWindow(window,SW_RESTORE);featureResults["splitter_vertical_resize"]&=sidebarWidth==restoredWidth;
        dragControl(horizontalSplitter.window,0,55);GetWindowRect(treeHost,&afterTree);featureResults["splitter_horizontal_resize"]=afterTree.bottom-afterTree.top>beforeTree.bottom-beforeTree.top;
        dragControl(horizontalSplitter.window,0,5000);RECT clientRect;GetClientRect(window,&clientRect);featureResults["splitter_horizontal_resize"]&=playlistSplit<=clientRect.bottom-235;playlistFraction=savedFraction;layout();
        auto found=leaves.find(selectedPath);bool hierarchy=false;if(found!=leaves.end()){auto parent=TreeView_GetParent(tree,found->second);RECT leafRect{},groupRect{};TVITEMW group{};group.hItem=parent;group.mask=TVIF_PARAM;TreeView_GetItem(tree,&group);TreeView_EnsureVisible(tree,found->second);bool leafRectValid=TreeView_GetItemRect(tree,found->second,&leafRect,TRUE);TreeView_EnsureVisible(tree,parent);bool groupRectValid=TreeView_GetItemRect(tree,parent,&groupRect,TRUE);int leafTextLeft=leafRect.left,groupTextLeft=groupRect.left;hierarchy=parent&&group.lParam<0&&leafRectValid&&groupRectValid&&leafTextLeft>groupTextLeft;
            TreeView_EnsureVisible(tree,parent);TreeView_GetItemRect(tree,parent,&groupRect,FALSE);bool expanded=(TreeView_GetItemState(tree,parent,TVIS_EXPANDED)&TVIS_EXPANDED)!=0;SendMessageW(tree,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(14,(groupRect.top+groupRect.bottom)/2));bool toggled=((TreeView_GetItemState(tree,parent,TVIS_EXPANDED)&TVIS_EXPANDED)!=0)!=expanded;hierarchy&=toggled;TreeView_Expand(tree,parent,TVE_EXPAND);
            RECT treeClient;GetClientRect(tree,&treeClient);std::ofstream debug(root/L"validation"/L"ui_revision_hierarchy.json");debug<<"{\"parent_parameter\":"<<group.lParam<<",\"leaf_rect_valid\":"<<(leafRectValid?"true":"false")<<",\"group_rect_valid\":"<<(groupRectValid?"true":"false")<<",\"leaf_left\":"<<leafTextLeft<<",\"group_left\":"<<groupTextLeft<<",\"group_top\":"<<groupRect.top<<",\"group_bottom\":"<<groupRect.bottom<<",\"click_y\":"<<(groupRect.top+groupRect.bottom)/2<<",\"client_height\":"<<treeClient.bottom<<",\"chevron_toggled\":"<<(toggled?"true":"false")<<"}";
        }
        featureResults["hierarchy_indented"]=hierarchy;featureResults["static_backgrounds_match"]=checkStaticBackgrounds();featureResults["dark_scrollbars"]=checkScrollbars();
    }
    void runRevision(){
        if(current.phase==Phase::Error||Clock::now()-testStart>std::chrono::seconds(35)){finishTest(false);return;}
        if(testStage==0){select(skillFolder()/L"Swordman"/L"BloodyRave.rep");testStage=1;}
        else if(testStage==1&&current.phase==Phase::Playing&&current.elapsed>=700){SendMessageW(window,WM_COMMAND,NextFrame,0);testStage=2;}
        else if(testStage==2&&current.phase==Phase::Paused&&current.frameCount>0){
            revisionElapsed=current.elapsed;stageAt=Clock::now();int chosen=-1;for(size_t i=0;i<imageRows.size();i++)if(imageRows[i].drawn&&!imageRows[i].dependency){chosen=int(i);featureImg=imageRows[i].path;break;}if(chosen<0){finishTest(false);return;}
            ListView_SetCheckState(imgList,chosen,FALSE);captureSerial=current.captureSerial;worker->captureFrame();testStage=3;
        }
        else if(testStage==3&&current.captureSerial>captureSerial&&Clock::now()-stageAt>std::chrono::milliseconds(250)){
            featureResults["step_pauses"]=current.phase==Phase::Paused&&current.elapsed==revisionElapsed;featureCrc=current.frameCrc;revisionOrdinal=current.ordinal;captureSerial=current.captureSerial;SendMessageW(window,WM_COMMAND,NextFrame,0);testStage=4;
        }
        else if(testStage==4&&current.captureSerial>captureSerial){
            featureResults["step_next_adjacent"]=current.ordinal==revisionOrdinal+1&&current.phase==Phase::Paused;
            featureResults["step_hidden_retained"]=current.hiddenImages==hiddenImages&&current.hiddenImages.contains(featureImg);captureSerial=current.captureSerial;SendMessageW(window,WM_COMMAND,PreviousFrame,0);testStage=5;
        }
        else if(testStage==5&&current.captureSerial>captureSerial){
            featureResults["step_previous_restores"]=current.ordinal==revisionOrdinal&&current.frameCrc==featureCrc;featureResults["step_hidden_retained"]&=current.hiddenImages==hiddenImages;checkRevisionLayout();
            finishTest(std::all_of(featureResults.begin(),featureResults.end(),[](const auto& result){return result.second;}));testStage=6;
        }
    }
    void runImgFilters(){
        constexpr int removeBackgroundId=18,removeMonstersId=19,removeCharactersId=20;
        if(current.phase==Phase::Error||Clock::now()-testStart>std::chrono::seconds(30)){finishTest(false);return;}
        auto matches=[](const std::string& path,int id){if(id==removeBackgroundId)return path.starts_with("sprite/map/")||path.starts_with("sprite/background/");if(id==removeMonstersId)return path.starts_with("sprite/monster/")&&path.find("/effect/")==std::string::npos&&path.find("/effects/")==std::string::npos;return (path.starts_with("sprite/character/")||path.starts_with("sprite/npc/"))&&path.find("/effect/")==std::string::npos&&path.find("/effects/")==std::string::npos;};
        auto categoryHidden=[&](int id){bool found=false;for(const auto& call:current.allImages)if(matches(call.path,id)){found=true;if(!hiddenImages.contains(call.path))return false;}return found;};
        auto click=[&](int id){SendMessageW(GetDlgItem(window,id),BM_CLICK,0,0);captureSerial=current.captureSerial;worker->captureFrame();};
        if(testStage==0){
            bool present=true;for(int id:{removeBackgroundId,removeMonstersId,removeCharactersId})present&=IsWindowVisible(GetDlgItem(window,id))!=FALSE;
            featureResults["bulk_buttons_present"]=present;if(!present){finishTest(false);return;}
            featureResults["bulk_path_heuristics"]=matchesImageCategory("Sprite\\Map\\Village\\Floor.IMG",ImageCategory::Background)&&matchesImageCategory("background/studio.img",ImageCategory::Background)&&matchesImageCategory("sprite/monster/cartel/normalsoldier.img",ImageCategory::Monsters)&&matchesImageCategory("sprite/character/priest/atequipment/avatar/skin/pg_body0002.img",ImageCategory::Characters)&&matchesImageCategory("sprite/character/swordman/equipment/growtype/basics/ghosthand.img",ImageCategory::Characters)&&matchesImageCategory("sprite/npc/shopkeeper.img",ImageCategory::Characters)&&!matchesImageCategory("sprite/character/swordman/effect/darkflame/flame.img",ImageCategory::Characters)&&!matchesImageCategory("sprite/monster/cartel/effects/smoke.img",ImageCategory::Monsters)&&!matchesImageCategory("sprite/common/commoneffect/new_dust.img",ImageCategory::Characters)&&!matchesImageCategory("sprite/map/village/floor.npk",ImageCategory::Background);
            bool labels=controlText(GetDlgItem(window,removeBackgroundId))==L"移除背景"&&controlText(GetDlgItem(window,removeMonstersId))==L"移除怪物"&&controlText(GetDlgItem(window,removeCharactersId))==L"移除人物";SendMessageW(window,WM_COMMAND,LanguageEn,0);labels&=controlText(GetDlgItem(window,removeBackgroundId))==L"Hide BG"&&controlText(GetDlgItem(window,removeMonstersId))==L"Hide monsters"&&controlText(GetDlgItem(window,removeCharactersId))==L"Hide people";SendMessageW(window,WM_COMMAND,LanguageZh,0);featureResults["bulk_bilingual_labels"]=labels;
            int saved=sidebarWidth;sidebarWidth=300;layout();bool fit=true;RECT list{};GetWindowRect(imageHost,&list);RECT previous{};
            for(int id:{removeBackgroundId,removeMonstersId,removeCharactersId}){RECT r{};GetWindowRect(GetDlgItem(window,id),&r);fit&=r.right>r.left&&r.bottom<=list.top&&r.left>=previous.right;previous=r;}
            RECT clientRect{};GetClientRect(window,&clientRect);POINT edge{clientRect.right,0};ClientToScreen(window,&edge);fit&=previous.right<=edge.x;featureResults["bulk_buttons_fit_minimum_sidebar"]=fit;sidebarWidth=saved;layout();
            select(skillFolder()/L"Swordman"/L"BloodyRave.rep");testStage=1;
        }else if(testStage==1&&current.phase==Phase::Playing&&current.elapsed>=700&&current.frameCount){SendMessageW(window,WM_COMMAND,PlayPause,0);testStage=2;}
        else if(testStage==2&&current.phase==Phase::Paused){captureSerial=current.captureSerial;worker->captureFrame();testStage=3;}
        else if(testStage==3&&current.captureSerial>captureSerial){featureCrc=current.frameCrc;revisionElapsed=current.elapsed;click(removeBackgroundId);testStage=4;}
        else if(testStage==4&&current.captureSerial>captureSerial){featureResults["bulk_background_hidden"]=categoryHidden(removeBackgroundId)&&hiddenImages.contains("sprite/map/chn_pvp/chn_studio/map1_tile.img");featureResults["bulk_preview_changes"]=current.frameCrc!=featureCrc&&current.elapsed==revisionElapsed;featureResults["bulk_shared_visibility"]=current.hiddenImages==hiddenImages;click(removeMonstersId);testStage=5;}
        else if(testStage==5&&current.captureSerial>captureSerial){featureResults["bulk_monsters_hidden"]=categoryHidden(removeMonstersId)&&hiddenImages.contains("sprite/monster/cartel/normalsoldier.img");featureResults["bulk_shared_visibility"]&=current.hiddenImages==hiddenImages;click(removeCharactersId);testStage=6;}
        else if(testStage==6&&current.captureSerial>captureSerial){
            featureResults["bulk_characters_hidden"]=categoryHidden(removeCharactersId)&&hiddenImages.contains("sprite/character/swordman/equipment/avatar/skin/sm_body0001.img");featureResults["bulk_shared_visibility"]&=current.hiddenImages==hiddenImages;
            bool effects=true,hasEffect=false,whole=true,otherFrame=false;std::unordered_set<std::string> currentPaths;for(const auto& call:current.currentImages)currentPaths.insert(call.path);
            for(const auto& call:current.allImages){if(call.path.find("/effect/")!=std::string::npos||call.path.find("/effects/")!=std::string::npos){hasEffect=true;effects&=!hiddenImages.contains(call.path);}if(matches(call.path,removeBackgroundId)||matches(call.path,removeMonstersId)||matches(call.path,removeCharactersId)){imgFilterMatchingPaths++;whole&=hiddenImages.contains(call.path);otherFrame|=!currentPaths.contains(call.path);}}
            imgFilterWholeMatches=whole;imgFilterOtherFrameMatch=otherFrame;
            featureResults["bulk_effects_preserved"]=hasEffect&&effects;featureResults["bulk_whole_replay_paths"]=whole&&imgFilterMatchingPaths>0;
            SendMessageW(window,WM_COMMAND,AllImages,0);int chosen=-1;for(size_t i=0;i<imageRows.size();i++)if(imageRows[i].path=="sprite/character/swordman/equipment/avatar/skin/sm_body0001.img"){chosen=int(i);featureImg=imageRows[i].path;break;}
            if(chosen<0){finishTest(false);return;}ListView_SetCheckState(imgList,chosen,TRUE);captureSerial=current.captureSerial;worker->captureFrame();testStage=7;
        }else if(testStage==7&&current.captureSerial>captureSerial){featureResults["bulk_manual_checkbox_restore"]=!hiddenImages.contains(featureImg)&&!current.hiddenImages.contains(featureImg);SendMessageW(window,WM_COMMAND,ShowAll,0);captureSerial=current.captureSerial;worker->captureFrame();testStage=8;}
        else if(testStage==8&&current.captureSerial>captureSerial){featureResults["bulk_show_all_restores"]=hiddenImages.empty()&&current.hiddenImages.empty()&&current.frameCrc==featureCrc;click(removeBackgroundId);bool hidden=!hiddenImages.empty();select(skillFolder()/L"Swordman"/L"BloodSword.rep");featureResults["bulk_new_replay_resets"]=hidden&&hiddenImages.empty();testStage=9;}
        else if(testStage==9&&current.phase==Phase::Playing){featureResults["bulk_new_replay_resets"]&=current.hiddenImages.empty();finishTest(std::all_of(featureResults.begin(),featureResults.end(),[](const auto& r){return r.second;}));testStage=10;}
    }
    void runFeatures(){auto bloody=skillFolder()/L"Swordman"/L"BloodyRave.rep";if(current.phase==Phase::Error||Clock::now()-testStart>std::chrono::seconds(35)){finishTest(false);return;}
        if(testStage==0){select(bloody);testStage=1;}else if(testStage==1&&current.phase==Phase::Playing&&current.elapsed>=1050){SendMessageW(window,WM_COMMAND,PlayPause,0);testStage=2;}else if(testStage==2&&current.phase==Phase::Paused){captureSerial=current.captureSerial;worker->captureFrame();stageAt=Clock::now();testStage=3;}else if(testStage==3&&current.captureSerial>captureSerial){featureCrc=current.frameCrc;featureTimestamp=current.timestamp;captureSerial=current.captureSerial;testStage=4;}
        else if(testStage==4&&Clock::now()-stageAt>std::chrono::milliseconds(450)){featureResults["pause_freezes"]=current.timestamp==featureTimestamp&&current.phase==Phase::Paused;worker->captureFrame();testStage=5;}
        else if(testStage==5&&current.captureSerial>captureSerial){featureResults["pause_freezes"]&=current.frameCrc==featureCrc;int chosen=-1;for(size_t i=0;i<imageRows.size();i++)if(imageRows[i].drawn&&!imageRows[i].dependency){chosen=int(i);featureImg=imageRows[i].path;break;}if(chosen<0){finishTest(false);return;}ListView_SetCheckState(imgList,chosen,FALSE);captureSerial=current.captureSerial;worker->captureFrame();testStage=6;}
        else if(testStage==6&&current.captureSerial>captureSerial){featureResults["image_hide_changes_frame"]=current.frameCrc!=featureCrc;featureResults["hidden_remains_listed"]=std::any_of(imageRows.begin(),imageRows.end(),[&](const auto& row){return row.path==featureImg;});SendMessageW(window,WM_COMMAND,AllImages,0);int chosen=-1;for(size_t i=0;i<imageRows.size();i++)if(imageRows[i].path==featureImg){chosen=int(i);break;}featureResults["image_state_shared"]=chosen>=0&&!ListView_GetCheckState(imgList,chosen);if(chosen>=0)ListView_SetCheckState(imgList,chosen,TRUE);captureSerial=current.captureSerial;worker->captureFrame();testStage=7;}
        else if(testStage==7&&current.captureSerial>captureSerial){featureResults["image_restore_matches_frame"]=current.frameCrc==featureCrc;featureResults["search_zh"]=searchContains(L"嗜魂封魔斩",bloody);featureResults["search_en"]=searchContains(L"BloodyRave",bloody);featureResults["search_vp"]=searchContains(L"猩红旋涡",skillFolder()/L"Swordman"/L"BloodyRave_VP1.rep");SetWindowTextW(search,L"BloodyRave");auto selected=selectedPath;SendMessageW(window,WM_COMMAND,LanguageEn,0);bool preserved=controlText(search)==L"BloodyRave"&&selectedPath==selected&&leaves.contains(bloody);SendMessageW(window,WM_COMMAND,LanguageZh,0);featureResults["language_preserves_search_selection"]=preserved&&controlText(search)==L"BloodyRave"&&selectedPath==selected;featureResults["client_roots"]=current.client==client&&folder==client/L"Replay";SetWindowTextW(search,L"");SendMessageW(window,WM_COMMAND,CurrentImages,0);SendMessageW(window,WM_COMMAND,PlayPause,0);testStage=8;stageAt=Clock::now();}
        else if(testStage==8&&Clock::now()-stageAt>std::chrono::milliseconds(400)){featureResults["resume_advances"]=current.timestamp>featureTimestamp&&current.phase==Phase::Playing;finishTest(std::all_of(featureResults.begin(),featureResults.end(),[](const auto& r){return r.second;}));testStage=9;}
    }
    void timer(){current=worker->status();updateStatus();updateImages();treeScroll.refresh();imageScroll.refresh();if(!test)return;if(imgFilterTest){runImgFilters();return;}if(inputTest){runInput();return;}if(revisionTest){runRevision();return;}if(featureTest){runFeatures();return;}if(testStage==-2&&current.phase==Phase::Error){recovered=true;testStage=0;}if((current.phase==Phase::Error&&testStage!=0&&testStage!=-2)||Clock::now()-testStart>std::chrono::seconds(55)){finishTest(false);return;}
        if(testStage==-1){worker->open(badPath);testStage=-2;}else if(testStage==0){select(benchmark?skillFolder()/L"Priest"/L"DivinePunishment.rep":skillFolder()/L"Swordman"/L"BloodSword.rep");testStage=1;}else if(testStage==1&&current.phase==Phase::Ended&&current.frozen){first=current;worker->replay();testStage=2;}else if(testStage==2&&current.phase==Phase::Playing){replayed=current.timestamp<first.timestamp;testStage=3;}else if(testStage==3&&current.phase==Phase::Ended&&current.frozen){hot=current;select(benchmark?skillFolder()/L"Gunner"/L"ReturnedSniper.rep":skillFolder()/L"Priest"/L"DivinePunishment.rep");testStage=4;}else if(testStage==4&&current.phase==Phase::Playing){switched=true;testStage=5;}else if(testStage==5&&current.phase==Phase::Ended&&current.frozen){second=current;finishTest(true);testStage=6;}}
};
LRESULT CALLBACK treeInput(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR context){
    auto* app=reinterpret_cast<Application*>(context);
    if(message==WM_LBUTTONDOWN){
        // Custom rows include a heading, filename and space outside the native
        // label rectangle. Resolve the whole painted row for groups and REP files.
        int y=GET_Y_LPARAM(lp);RECT client;GetClientRect(window,&client);
        for(auto hit=TreeView_GetFirstVisible(window);hit;hit=TreeView_GetNextVisible(window,hit)){
            RECT row{};if(!TreeView_GetItemRect(window,hit,&row,FALSE))continue;if(row.top>=client.bottom)break;
            if(y>=row.top&&y<row.bottom){TVITEMW item{};item.mask=TVIF_PARAM;item.hItem=hit;TreeView_GetItem(window,&item);if(item.lParam<0){SetFocus(window);TreeView_Expand(window,hit,TVE_TOGGLE);app->treeScroll.refresh();return 0;}if(item.lParam>0&&size_t(item.lParam)<=app->catalog->items().size()){SetFocus(window);app->select(app->catalog->items()[size_t(item.lParam)-1].path);app->treeScroll.refresh();return 0;}break;}
        }
    }
    return DefSubclassProc(window,message,wp,lp);
}
LRESULT CALLBACK canvasInput(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR context){
    auto* app=reinterpret_cast<Application*>(context);
    if(message==WM_LBUTTONDOWN){SetFocus(window);return 0;}
    if(message==WM_GETDLGCODE)return DLGC_WANTARROWS|DLGC_WANTCHARS;
    if(message==WM_KEYDOWN&&(wp==VK_LEFT||wp==VK_RIGHT)){SendMessageW(app->window,WM_COMMAND,wp==VK_LEFT?PreviousFrame:NextFrame,0);return 0;}
    return DefSubclassProc(window,message,wp,lp);
}
bool CanvasDialog::read(rep::CanvasSettings& settings){
    settings=draft;
    auto number=[&](HWND control,int& target,bool required){
        auto value=controlText(control);size_t consumed=0;
        try{auto parsed=std::stoll(value,&consumed);if(consumed==value.size()&&parsed>=0&&parsed<=std::numeric_limits<int>::max()){target=int(parsed);return true;}}catch(const std::exception&){}
        return !required;
    };
    bool size=draft.mode==rep::CanvasMode::Size,padding=draft.mode==rep::CanvasMode::Padding;
    bool valid=number(width,settings.width,size)&number(height,settings.height,size)&number(left,settings.left,padding)&number(top,settings.top,padding)&number(right,settings.right,padding)&number(bottom,settings.bottom,padding);
    if(!valid){text(error,app->t(L"请输入有效的非负整数。",L"Enter a valid non-negative integer."));return false;}
    try{settings.resolve(app->current.width,app->current.height);text(error,L"");return true;}
    catch(const std::exception& e){text(error,app->t(L"画布设置无效：",L"Invalid canvas settings: ")+rep::wide(e.what()));return false;}
}
void CanvasDialog::update(){
    for(auto control:multipleControls)ShowWindow(control,draft.mode==rep::CanvasMode::Multiple?SW_SHOW:SW_HIDE);
    for(auto control:sizeControls)ShowWindow(control,draft.mode==rep::CanvasMode::Size?SW_SHOW:SW_HIDE);
    for(auto control:paddingControls)ShowWindow(control,draft.mode==rep::CanvasMode::Padding?SW_SHOW:SW_HIDE);
    rep::CanvasSettings settings;bool valid=read(settings);EnableWindow(GetDlgItem(window,CanvasApply),valid);EnableWindow(GetDlgItem(window,IDOK),valid);
    if(valid){auto layout=settings.resolve(app->current.width,app->current.height);text(result,app->t(L"实际画布：",L"Canvas size: ")+std::to_wstring(layout.width)+L" × "+std::to_wstring(layout.height));text(origin,app->t(L"原画位置：左 ",L"Original picture: left ")+std::to_wstring(layout.left)+app->t(L" px，上 ",L" px, top ")+std::to_wstring(layout.top)+L" px");if(draft.mode==rep::CanvasMode::Size&&(settings.width<app->current.width||settings.height<app->current.height))text(error,app->t(L"实际画布至少为当前 REP 原始尺寸，填写的目标尺寸保留。",L"Canvas is at least the REP size; your target dimensions are retained."));}
    else{text(result,app->t(L"实际画布：—",L"Canvas size: —"));text(origin,L"");}
    for(int id:{ModeMultiple,ModeSize,ModePadding,FactorOne,FactorOneHalf,FactorTwo,FactorThree})InvalidateRect(GetDlgItem(window,id),nullptr,FALSE);
}
void CanvasDialog::fill(){filling=true;text(width,std::to_wstring(draft.width));text(height,std::to_wstring(draft.height));text(left,std::to_wstring(draft.left));text(top,std::to_wstring(draft.top));text(right,std::to_wstring(draft.right));text(bottom,std::to_wstring(draft.bottom));filling=false;update();}
bool CanvasDialog::apply(){rep::CanvasSettings settings;if(!read(settings))return false;draft=settings;app->applyCanvasSettings(settings);update();return true;}
void CanvasDialog::create(){WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.lpfnWndProc=canvasProcedure;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=app->surfaceBrush;cls.lpszClassName=L"RepCanvasSettings";RegisterClassExW(&cls);RECT r;GetWindowRect(owner,&r);window=CreateWindowExW(WS_EX_DLGMODALFRAME,cls.lpszClassName,app->t(L"画布设置",L"Canvas settings").c_str(),WS_POPUP|WS_CAPTION|WS_SYSMENU,(r.left+r.right-620)/2,(r.top+r.bottom-540)/2,620,540,owner,nullptr,GetModuleHandleW(nullptr),this);}
LRESULT CALLBACK canvasProcedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* d=reinterpret_cast<CanvasDialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){d=static_cast<CanvasDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);d->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(d));}
    if(!d)return DefWindowProcW(window,message,wp,lp);auto& app=*d->app;
    switch(message){
    case WM_CREATE:{
        auto add=[&](const wchar_t* cls,const std::wstring& title,DWORD style,int id,int x,int y,int w,int h){auto control=app.child(cls,title.c_str(),style,id,window);MoveWindow(control,x,y,w,h,TRUE);return control;};
        add(L"STATIC",app.t(L"REP 原始尺寸：",L"REP original size: ")+std::to_wstring(app.current.width)+L" × "+std::to_wstring(app.current.height),SS_LEFT,CanvasOriginal,20,20,565,26);
        add(L"BUTTON",app.t(L"按倍数",L"Multiplier"),BS_OWNERDRAW|WS_TABSTOP,ModeMultiple,20,64,175,34);add(L"BUTTON",app.t(L"指定宽高",L"Width / height"),BS_OWNERDRAW|WS_TABSTOP,ModeSize,212,64,175,34);add(L"BUTTON",app.t(L"四边留边",L"Edge padding"),BS_OWNERDRAW|WS_TABSTOP,ModePadding,404,64,175,34);
        for(int i=0;i<4;i++){const wchar_t* labels[]={L"1×",L"1.5×",L"2×",L"3×"};d->multipleControls.push_back(add(L"BUTTON",labels[i],BS_OWNERDRAW|WS_TABSTOP,FactorOne+i,20+i*145,128,126,34));}
        d->multipleControls.push_back(add(L"STATIC",app.t(L"四周扩展，原画面居中。",L"Extend all sides; keep the original picture centered."),SS_LEFT,0,20,182,565,26));
        auto field=[&](std::vector<HWND>& controls,const std::wstring& label,int id,int x,int y,int w){controls.push_back(add(L"STATIC",label,SS_LEFT,0,x,y,w,22));auto edit=add(L"EDIT",L"",ES_NUMBER|ES_AUTOHSCROLL|WS_BORDER|WS_TABSTOP,id,x,y+27,w,30);controls.push_back(edit);return edit;};
        d->width=field(d->sizeControls,app.t(L"宽（px）",L"Width (px)"),CanvasWidth,20,123,265);d->height=field(d->sizeControls,app.t(L"高（px）",L"Height (px)"),CanvasHeight,314,123,265);d->sizeControls.push_back(add(L"STATIC",app.t(L"原画面居中；实际尺寸至少为 REP 原始尺寸。",L"Center the original picture; canvas is at least the REP size."),SS_LEFT,0,20,207,565,36));
        d->left=field(d->paddingControls,app.t(L"左（px）",L"Left (px)"),CanvasLeft,20,115,265);d->top=field(d->paddingControls,app.t(L"上（px）",L"Top (px)"),CanvasTop,314,115,265);d->right=field(d->paddingControls,app.t(L"右（px）",L"Right (px)"),CanvasRight,20,188,265);d->bottom=field(d->paddingControls,app.t(L"下（px）",L"Bottom (px)"),CanvasBottom,314,188,265);
        d->error=add(L"STATIC",L"",SS_LEFT,CanvasError,20,260,565,35);d->result=add(L"STATIC",L"",SS_LEFT,CanvasResult,20,302,565,24);d->origin=add(L"STATIC",L"",SS_LEFT,CanvasOrigin,20,333,565,24);add(L"STATIC",app.t(L"摄像机、素材坐标和比例保持不变。\n预览与导出共用画布；窗口适配只影响预览显示大小。",L"Camera, sprite coordinates and scale stay unchanged.\nPreview and export share the canvas; window fitting affects preview only."),SS_LEFT,0,20,372,565,42);
        add(L"BUTTON",app.t(L"恢复原始",L"Reset to original"),BS_OWNERDRAW|WS_TABSTOP,CanvasRestore,20,438,132,34);add(L"BUTTON",app.t(L"取消",L"Cancel"),BS_OWNERDRAW|WS_TABSTOP,IDCANCEL,269,438,90,34);add(L"BUTTON",app.t(L"应用",L"Apply"),BS_OWNERDRAW|WS_TABSTOP,CanvasApply,369,438,90,34);add(L"BUTTON",app.t(L"确定",L"OK"),BS_OWNERDRAW|WS_TABSTOP,IDOK,469,438,110,34);d->fill();SetFocus(GetDlgItem(window,ModeMultiple+int(d->draft.mode)));return 0;}
    case WM_COMMAND:{int id=LOWORD(wp);
        if(id>=ModeMultiple&&id<=ModePadding){rep::CanvasSettings settings;if(d->read(settings))d->draft=settings;d->draft.mode=rep::CanvasMode(id-ModeMultiple);d->update();}
        else if(id>=FactorOne&&id<=FactorThree){const double factors[]={1,1.5,2,3};d->draft.factor=factors[id-FactorOne];d->update();}
        else if(id>=CanvasWidth&&id<=CanvasBottom&&HIWORD(wp)==EN_CHANGE&&!d->filling)d->update();
        else if(id==CanvasRestore){d->draft.mode=rep::CanvasMode::Multiple;d->draft.factor=1;d->draft.left=d->draft.top=d->draft.right=d->draft.bottom=0;d->fill();}
        else if(id==CanvasApply)d->apply();else if(id==IDOK){if(d->apply())DestroyWindow(window);}else if(id==IDCANCEL)DestroyWindow(window);return 0;}
    case WM_DRAWITEM:{int id=int(wp);const double factors[]={1,1.5,2,3};bool active=(id>=ModeMultiple&&id<=ModePadding&&int(d->draft.mode)==id-ModeMultiple)||(id>=FactorOne&&id<=FactorThree&&d->draft.factor==factors[id-FactorOne]);drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp),active);return TRUE;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:SetBkColor(reinterpret_cast<HDC>(wp),Surface);SetBkMode(reinterpret_cast<HDC>(wp),TRANSPARENT);SetTextColor(reinterpret_cast<HDC>(wp),Text);return reinterpret_cast<LRESULT>(app.surfaceBrush);
    case WM_CTLCOLOREDIT:SetBkColor(reinterpret_cast<HDC>(wp),RGB(28,30,35));SetTextColor(reinterpret_cast<HDC>(wp),Text);return reinterpret_cast<LRESULT>(app.inputBrush);
    case WM_CLOSE:DestroyWindow(window);return 0;case WM_DESTROY:d->done=true;return 0;
    }return DefWindowProcW(window,message,wp,lp);
}
void AudioPopup::show(){
    if(window){DestroyWindow(window);return;}
    WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.lpfnWndProc=audioProcedure;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=app->surfaceBrush;cls.lpszClassName=L"RepAudioVolume";RegisterClassExW(&cls);
    RECT r;GetWindowRect(app->volumeButton,&r);window=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,app->t(L"预览音量",L"Preview volume").c_str(),WS_POPUP|WS_CAPTION|WS_SYSMENU,r.right-270,r.top-125,270,125,app->window,nullptr,GetModuleHandleW(nullptr),this);ShowWindow(window,SW_SHOW);
}
LRESULT CALLBACK audioProcedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* d=reinterpret_cast<AudioPopup*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){d=static_cast<AudioPopup*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);d->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(d));}
    if(!d)return DefWindowProcW(window,message,wp,lp);auto& app=*d->app;
    switch(message){
    case WM_CREATE:d->level=app.child(L"STATIC",L"",SS_LEFT,402,window);MoveWindow(d->level,20,12,220,24,TRUE);d->slider=app.child(TRACKBAR_CLASSW,L"",TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,401,window);MoveWindow(d->slider,15,43,230,30,TRUE);SendMessageW(d->slider,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));SendMessageW(d->slider,TBM_SETPOS,TRUE,app.audioVolume);app.updateAudioControls();return 0;
    case WM_HSCROLL:if(reinterpret_cast<HWND>(lp)==d->slider)app.setAudioVolume(int(SendMessageW(d->slider,TBM_GETPOS,0,0)));return 0;
    case WM_CTLCOLORSTATIC:SetBkColor(reinterpret_cast<HDC>(wp),Surface);SetBkMode(reinterpret_cast<HDC>(wp),TRANSPARENT);SetTextColor(reinterpret_cast<HDC>(wp),Text);return reinterpret_cast<LRESULT>(app.surfaceBrush);
    case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE)DestroyWindow(window);return 0;
    case WM_KEYDOWN:if(wp!=VK_ESCAPE)break;[[fallthrough]];
    case WM_CLOSE:DestroyWindow(window);return 0;
    case WM_DESTROY:d->window=d->slider=d->level=nullptr;return 0;
    }return DefWindowProcW(window,message,wp,lp);
}
void ExportDialog::update(){auto layout=app->canvasSettings.resolve(app->current.width,app->current.height);text(sizeInfo,app->t(L"原始：",L"Original: ")+std::to_wstring(app->current.width)+L" × "+std::to_wstring(app->current.height)+app->t(L"\n画布：",L"\nCanvas: ")+std::to_wstring(layout.width)+L" × "+std::to_wstring(layout.height));int choice=ComboBox_GetCurSel(format);bool mp4=choice==1,png=choice==2;EnableWindow(alpha,!mp4);if(mp4)Button_SetCheck(alpha,BST_UNCHECKED);EnableWindow(audio,!png);Button_SetCheck(audio,png?BST_UNCHECKED:(options.audio?BST_CHECKED:BST_UNCHECKED));text(audioHint,png?app->t(L"PNG 只导出图片序列，不生成音轨或 WAV。",L"PNG exports images only; no audio track or WAV."):app->t(L"仅导出 REP 记录的声音；预览音量不影响导出。",L"Recorded REP sound only; preview volume does not affect export."));text(description,choice==0?app->t(L"推荐剪辑素材：ProRes 4444，支持 Alpha。",L"Editing media: ProRes 4444 with alpha support."):choice==1?app->t(L"H.264 不透明预览视频，适合分享。",L"Opaque H.264 preview video for sharing."):app->t(L"逐帧无损 RGBA 图片序列。",L"Lossless RGBA image sequence."));text(alphaHint,mp4?app->t(L"MP4 不保留透明通道，此选项已禁用。",L"MP4 has no alpha channel; transparency is disabled."):app->t(L"透明底只清空底色；地图 IMG 需在资源面板取消勾选。",L"Transparent canvas clears the base; uncheck map IMG in the resource panel."));}
void ExportDialog::create(){WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.lpfnWndProc=exportProcedure;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=app->surfaceBrush;cls.lpszClassName=L"RepExportSettings";RegisterClassExW(&cls);RECT r;GetWindowRect(app->window,&r);window=CreateWindowExW(WS_EX_DLGMODALFRAME,cls.lpszClassName,app->t(L"导出素材",L"Export media").c_str(),WS_POPUP|WS_CAPTION|WS_SYSMENU,(r.left+r.right-520)/2,(r.top+r.bottom-710)/2,520,710,app->window,nullptr,GetModuleHandleW(nullptr),this);}
void ExportDialog::submit(){options.format=ComboBox_GetCurSel(format)==1?rep::ExportFormat::Mp4:ComboBox_GetCurSel(format)==2?rep::ExportFormat::Png:rep::ExportFormat::Mov;options.fps=ComboBox_GetCurSel(fps)==1?30:60;options.alpha=options.format!=rep::ExportFormat::Mp4&&Button_GetCheck(alpha)==BST_CHECKED;options.audio=options.format!=rep::ExportFormat::Png&&Button_GetCheck(audio)==BST_CHECKED;options.outputDirectory=controlText(directory);options.fileName=controlText(name);if(options.outputDirectory.empty()||options.fileName.empty()){MessageBoxW(window,app->t(L"请填写输出目录和文件名。",L"Enter an output folder and filename.").c_str(),L"REP Player",MB_OK|MB_ICONERROR);return;}accepted=true;DestroyWindow(window);}
LRESULT CALLBACK exportProcedure(HWND window,UINT message,WPARAM wp,LPARAM lp){auto* d=reinterpret_cast<ExportDialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){d=static_cast<ExportDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);d->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(d));}if(!d)return DefWindowProcW(window,message,wp,lp);auto& app=*d->app;
    switch(message){case WM_CREATE:{auto add=[&](const wchar_t* cls,const std::wstring& title,DWORD style,int id,int x,int y,int w,int h){auto c=app.child(cls,title.c_str(),style,id,window);MoveWindow(c,x,y,w,h,TRUE);return c;};auto selected=app.selectedItem();add(L"STATIC",selected?Catalog::display(*selected,app.english)+L" · "+app.selectedPath.filename().wstring():app.selectedPath.filename().wstring(),SS_LEFT,0,20,20,465,34);add(L"STATIC",app.t(L"素材格式",L"Media format"),0,0,20,66,465,22);d->format=add(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,101,20,93,465,130);ComboBox_AddString(d->format,L"MOV · ProRes 4444");ComboBox_AddString(d->format,L"MP4 · H.264");ComboBox_AddString(d->format,app.t(L"PNG · RGBA 序列",L"PNG · RGBA sequence").c_str());ComboBox_SetCurSel(d->format,0);d->description=add(L"STATIC",L"",SS_LEFT,0,20,132,465,34);
        add(L"STATIC",app.t(L"固定帧率",L"Constant frame rate"),0,0,20,180,220,22);add(L"STATIC",app.t(L"画面尺寸",L"Frame size"),0,0,264,180,220,22);d->fps=add(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,102,20,207,220,100);ComboBox_AddString(d->fps,L"60 FPS");ComboBox_AddString(d->fps,L"30 FPS");ComboBox_SetCurSel(d->fps,0);d->sizeInfo=add(L"STATIC",L"",SS_LEFT,108,264,207,220,42);add(L"BUTTON",app.t(L"画布设置…",L"Canvas settings…"),BS_OWNERDRAW|WS_TABSTOP,107,264,254,220,28);d->alpha=add(L"BUTTON",app.t(L"透明底 / 保留 Alpha",L"Transparent canvas / Keep alpha"),BS_AUTOCHECKBOX|WS_TABSTOP,103,20,293,465,27);Button_SetCheck(d->alpha,BST_CHECKED);d->alphaHint=add(L"STATIC",L"",SS_LEFT,0,20,328,465,40);d->audio=add(L"BUTTON",app.t(L"包含 REP 声音",L"Include recorded REP sound"),BS_AUTOCHECKBOX|WS_TABSTOP,109,20,380,465,27);add(L"STATIC",app.t(L"采用当前 IMG 勾选状态 · 已隐藏 ",L"Use current IMG visibility · Hidden ")+std::to_wstring(app.hiddenImages.size()),SS_LEFT,0,20,414,465,22);
        add(L"STATIC",app.t(L"输出目录",L"Output folder"),0,0,20,450,465,22);d->directory=add(L"EDIT",d->options.outputDirectory.wstring(),ES_AUTOHSCROLL|WS_BORDER|WS_TABSTOP,104,20,477,379,29);add(L"BUTTON",app.t(L"浏览…",L"Browse…"),BS_OWNERDRAW|WS_TABSTOP,105,407,477,78,29);add(L"STATIC",app.t(L"文件名（扩展名自动添加）",L"Filename (extension added automatically)"),0,0,20,519,465,22);d->name=add(L"EDIT",d->options.fileName,ES_AUTOHSCROLL|WS_BORDER|WS_TABSTOP,106,20,546,465,29);d->audioHint=add(L"STATIC",L"",SS_LEFT,110,20,582,465,28);add(L"BUTTON",app.t(L"取消",L"Cancel"),BS_OWNERDRAW|WS_TABSTOP,IDCANCEL,285,618,86,32);add(L"BUTTON",app.t(L"开始导出",L"Export"),BS_OWNERDRAW|WS_TABSTOP,IDOK,382,618,103,32);d->update();SetFocus(d->format);return 0;}
    case WM_COMMAND:if(LOWORD(wp)==101&&HIWORD(wp)==CBN_SELCHANGE){if(ComboBox_GetCurSel(d->format)!=1)Button_SetCheck(d->alpha,BST_CHECKED);d->update();}else if(LOWORD(wp)==109){d->options.audio=Button_GetCheck(d->audio)==BST_CHECKED;}else if(LOWORD(wp)==107){app.showCanvas(window);d->update();}else if(LOWORD(wp)==105){std::filesystem::path path;if(chooseDirectory(window,controlText(d->directory),app.t(L"选择导出目录",L"Select export folder").c_str(),path))text(d->directory,path.wstring());}else if(LOWORD(wp)==IDOK)d->submit();else if(LOWORD(wp)==IDCANCEL)DestroyWindow(window);return 0;
    case WM_DRAWITEM:drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp),wp==IDOK);return TRUE;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:SetBkColor(reinterpret_cast<HDC>(wp),Surface);SetBkMode(reinterpret_cast<HDC>(wp),TRANSPARENT);SetTextColor(reinterpret_cast<HDC>(wp),Text);return reinterpret_cast<LRESULT>(app.surfaceBrush);
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:SetTextColor(reinterpret_cast<HDC>(wp),Text);SetBkColor(reinterpret_cast<HDC>(wp),RGB(28,30,35));return reinterpret_cast<LRESULT>(app.inputBrush);
    case WM_CLOSE:DestroyWindow(window);return 0;case WM_DESTROY:d->done=true;return 0;
    }return DefWindowProcW(window,message,wp,lp);
}
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp){auto* app=reinterpret_cast<Application*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){app=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}if(!app)return DefWindowProcW(window,message,wp,lp);
    switch(message){case WM_CREATE:{app->font=CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");app->smallFont=CreateFontW(-11,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");app->groupFont=CreateFontW(-13,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");app->backgroundBrush=CreateSolidBrush(Background);app->surfaceBrush=CreateSolidBrush(Surface);app->inputBrush=CreateSolidBrush(RGB(28,30,35));auto c=[&](const wchar_t* cls,const wchar_t* title,DWORD style,int id=0){return app->child(cls,title,style,id);};
        app->clientTitle=c(L"STATIC",L"",SS_LEFT);app->pathLabel=c(L"STATIC",app->client.wstring().c_str(),SS_LEFT|SS_PATHELLIPSIS);app->choose=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,Choose);app->openButton=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,OpenRep);app->canvasButton=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,CanvasOptions);app->refreshButton=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,RefreshNpk);app->zhButton=c(L"BUTTON",L"中文",BS_OWNERDRAW|WS_TABSTOP,LanguageZh);app->enButton=c(L"BUTTON",L"EN",BS_OWNERDRAW|WS_TABSTOP,LanguageEn);app->canvas=c(L"STATIC",L"",SS_BLACKRECT,Canvas);app->playlistTitle=c(L"STATIC",L"",SS_LEFT);app->playlistCount=c(L"STATIC",L"",SS_RIGHT);app->search=c(L"EDIT",L"",ES_AUTOHSCROLL|WS_BORDER|WS_TABSTOP,Search);app->treeHost=createClippedPane(window,1010);app->tree=app->child(WC_TREEVIEWW,L"",TVS_NOHSCROLL|TVS_SHOWSELALWAYS|TVS_INFOTIP|WS_TABSTOP,Playlist,app->treeHost);SetWindowSubclass(app->tree,treeInput,1,reinterpret_cast<DWORD_PTR>(app));TreeView_SetBkColor(app->tree,Surface);TreeView_SetTextColor(app->tree,Text);TreeView_SetLineColor(app->tree,Muted);TreeView_SetItemHeight(app->tree,39);TreeView_SetIndent(app->tree,22);app->playlistFooter=c(L"STATIC",L"",SS_LEFT);
        app->imgTitle=c(L"STATIC",L"",SS_LEFT);app->imgCount=c(L"STATIC",L"",SS_RIGHT);app->imgCurrent=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,CurrentImages);app->imgAll=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,AllImages);app->imgShowAll=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,ShowAll);app->imgCaption=c(L"STATIC",L"",SS_LEFT|SS_ENDELLIPSIS);app->imageHost=createClippedPane(window,1011);app->imgList=app->child(WC_LISTVIEWW,L"",LVS_REPORT|LVS_NOCOLUMNHEADER|LVS_SHOWSELALWAYS|WS_TABSTOP,Images,app->imageHost);ListView_SetExtendedListViewStyle(app->imgList,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_INFOTIP);ListView_SetBkColor(app->imgList,Surface);ListView_SetTextBkColor(app->imgList,Surface);ListView_SetTextColor(app->imgList,Text);LVCOLUMNW col{};col.mask=LVCF_TEXT|LVCF_WIDTH;col.cx=220;col.pszText=const_cast<wchar_t*>(L"IMG");ListView_InsertColumn(app->imgList,0,&col);col.cx=98;col.pszText=const_cast<wchar_t*>(L"Frames");ListView_InsertColumn(app->imgList,1,&col);app->imageHeight=ImageList_Create(1,36,ILC_COLOR32,1,1);ListView_SetImageList(app->imgList,app->imageHeight,LVSIL_SMALL);app->imgDetail=c(L"STATIC",L"",SS_LEFT);app->imgFooter=c(L"STATIC",L"",SS_LEFT|SS_ENDELLIPSIS);
        app->currentTitle=c(L"STATIC",L"",SS_LEFT|SS_ENDELLIPSIS);app->currentJob=c(L"STATIC",L"",SS_RIGHT);app->playButton=c(L"BUTTON",L"▶",BS_OWNERDRAW|WS_TABSTOP,PlayPause);app->stopButton=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,Stop);app->replayButton=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,Replay);app->previousButton=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,PreviousFrame);app->nextButton=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,NextFrame);app->frameLabel=c(L"STATIC",L"",SS_RIGHT);app->muteButton=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,AudioMute);app->volumeButton=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,AudioVolume);app->audioPopup.app=app;app->clockLabel=c(L"STATIC",L"",SS_LEFT);app->exportButton=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,Export);app->label=c(L"STATIC",L"",SS_LEFT|SS_ENDELLIPSIS,StatusLabel);for(auto w:{app->playlistCount,app->playlistFooter,app->imgCount,app->imgCaption,app->imgDetail,app->imgFooter,app->label,app->clockLabel,app->currentJob,app->frameLabel})SendMessageW(w,WM_SETFONT,reinterpret_cast<WPARAM>(app->smallFont),TRUE);
        app->imgRemoveBackground=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,RemoveBackground);app->imgRemoveMonsters=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,RemoveMonsters);app->imgRemoveCharacters=c(L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,RemoveCharacters);SetWindowSubclass(app->canvas,canvasInput,1,reinterpret_cast<DWORD_PTR>(app));app->treeScroll.create(window,app->tree,true,1012);app->imageScroll.create(window,app->imgList,false,1013);app->verticalSplitter.create(window,true,1014,[app](int x){RECT r;GetClientRect(app->window,&r);app->sidebarWidth=r.right-x;app->layout();},[app]{app->saveSettings();});app->horizontalSplitter.create(window,false,1015,[app](int y){RECT r;GetClientRect(app->window,&r);int split=std::clamp(y,274,std::max(274,int(r.bottom)-ImagePanelMinimum));app->playlistFraction=double(split-44)/std::max(1L,r.bottom-44);app->layout();},[app]{app->saveSettings();});app->catalog=std::make_unique<Catalog>();app->folder=app->client.empty()?std::filesystem::path{}:app->client/L"Replay";app->catalog->scan(app->folder);app->worker=std::make_unique<PlaybackController>(app->canvas,app->root,app->client,app->test,app->testStart,app->protocol,app->canvasSettings);app->worker->setAudioVolume(app->audioVolume/100.f);app->worker->setAudioMuted(app->audioMuted);app->language();app->layout();SetTimer(window,1,75,nullptr);if(!app->openPath.empty()){if(!app->clientReady())app->chooseClient();if(app->clientReady())app->openSpecified(app->openPath);}return 0;}
    case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={1040,720};return 0;
    case WM_SIZE:if(app->tree&&wp!=SIZE_MINIMIZED)app->layout();return 0;
    case WM_COMMAND:switch(LOWORD(wp)){case Choose:app->chooseClient();break;case OpenRep:app->chooseReplay();break;case CanvasOptions:app->showCanvas();break;case RefreshNpk:app->refreshNpk();break;case AudioMute:app->toggleAudioMute();break;case AudioVolume:app->audioPopup.show();break;case Replay:app->worker->replay();break;case PreviousFrame:app->worker->stepFrame(-1);break;case NextFrame:app->worker->stepFrame(1);break;case Stop:app->worker->stop();break;case PlayPause:app->worker->togglePause();break;case LanguageZh:app->english=false;app->language();break;case LanguageEn:app->english=true;app->language();break;case Search:if(HIWORD(wp)==EN_CHANGE&&app->catalog)app->populate();break;case CurrentImages:app->allImg=false;app->updateImages(true);InvalidateRect(app->imgCurrent,nullptr,FALSE);InvalidateRect(app->imgAll,nullptr,FALSE);break;case AllImages:app->allImg=true;app->updateImages(true);InvalidateRect(app->imgCurrent,nullptr,FALSE);InvalidateRect(app->imgAll,nullptr,FALSE);break;case ShowAll:app->hiddenImages.clear();app->worker->setHiddenImages({});app->updateImages(true);break;case RemoveBackground:app->hideImageCategory(ImageCategory::Background);break;case RemoveMonsters:app->hideImageCategory(ImageCategory::Monsters);break;case RemoveCharacters:app->hideImageCategory(ImageCategory::Characters);break;case Export:app->showExport();break;}return 0;
    case WM_NOTIFY:{auto* h=reinterpret_cast<NMHDR*>(lp);if(h->hwndFrom==app->tree){if(h->code==TVN_SELCHANGEDW&&!app->rebuilding){auto i=reinterpret_cast<NMTREEVIEWW*>(lp)->itemNew.lParam;if(i>0&&size_t(i)<=app->catalog->items().size())app->open(app->catalog->items()[i-1].path);}else if(h->code==TVN_GETINFOTIPW){auto* tip=reinterpret_cast<NMTVGETINFOTIPW*>(lp);if(tip->lParam>0&&size_t(tip->lParam)<=app->catalog->items().size()){auto& item=app->catalog->items()[tip->lParam-1];auto full=Catalog::display(item,false)+L"\n"+rep::wide(item.relativePath);wcsncpy_s(tip->pszText,tip->cchTextMax,full.c_str(),_TRUNCATE);}}else if(h->code==NM_CUSTOMDRAW)return app->drawTree(*reinterpret_cast<NMTVCUSTOMDRAW*>(lp));}else if(h->hwndFrom==app->imgList){if(h->code==LVN_ITEMCHANGED)app->imageChanged(*reinterpret_cast<NMLISTVIEW*>(lp));else if(h->code==LVN_GETINFOTIPW){auto* tip=reinterpret_cast<NMLVGETINFOTIPW*>(lp);if(tip->iItem>=0&&size_t(tip->iItem)<app->imageRows.size()){auto full=rep::wide(app->imageRows[tip->iItem].path)+L"\n"+app->imageRows[tip->iItem].metadata;wcsncpy_s(tip->pszText,tip->cchTextMax,full.c_str(),_TRUNCATE);}}else if(h->code==NM_CUSTOMDRAW){auto* d=reinterpret_cast<NMLVCUSTOMDRAW*>(lp);if(d->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;if(d->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){size_t i=d->nmcd.dwItemSpec;if(i<app->imageRows.size()){d->clrText=app->hiddenImages.contains(app->imageRows[i].path)?Muted:Text;d->clrTextBk=(d->nmcd.uItemState&CDIS_SELECTED)?Selected:Surface;}return CDRF_NEWFONT;}}}return 0;}
    case WM_DRAWITEM:{bool active=(wp==LanguageZh&&!app->english)||(wp==LanguageEn&&app->english)||(wp==CurrentImages&&!app->allImg)||(wp==AllImages&&app->allImg)||wp==PlayPause||wp==Export||(wp==AudioMute&&app->audioMuted);drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp),active);return TRUE;}
    case WM_CTLCOLORSTATIC:return reinterpret_cast<LRESULT>(app->staticBackground(reinterpret_cast<HWND>(lp),reinterpret_cast<HDC>(wp)));
    case WM_CTLCOLOREDIT:SetBkColor(reinterpret_cast<HDC>(wp),RGB(28,30,35));SetTextColor(reinterpret_cast<HDC>(wp),Text);return reinterpret_cast<LRESULT>(app->inputBrush);
    case WM_ERASEBKGND:{RECT r;GetClientRect(window,&r);FillRect(reinterpret_cast<HDC>(wp),&r,app->backgroundBrush);RECT side{app->sidebarX,44,r.right,r.bottom};FillRect(reinterpret_cast<HDC>(wp),&side,app->surfaceBrush);return 1;}
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(window,&p);RECT progress{15,app->footerY+37,app->sidebarX-15,app->footerY+41};SetDCBrushColor(dc,RGB(65,66,70));FillRect(dc,&progress,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));if(app->current.duration){progress.right=progress.left+int((progress.right-progress.left)*std::clamp(double(app->current.elapsed)/app->current.duration,0.,1.));SetDCBrushColor(dc,Yellow);FillRect(dc,&progress,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));}EndPaint(window,&p);return 0;}
    case WM_TIMER:app->timer();return 0;
    case WM_CLOSE:KillTimer(window,1);app->cancelExport=true;app->worker.reset();DestroyWindow(window);if(wp)PostQuitMessage(1);return 0;case WM_DESTROY:PostQuitMessage(0);return 0;
    }return DefWindowProcW(window,message,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){try{CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TREEVIEW_CLASSES|ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&controls);Application app;app.root=rep::applicationDirectory();int argc;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);
    bool clientArgument=false;for(int i=1;i<argc;i++){auto arg=std::wstring_view(args[i]);if((arg==L"--ui-test"||arg==L"--ui-benchmark"||arg==L"--ui-feature-test"||arg==L"--ui-revision-test"||arg==L"--ui-input-test"||arg==L"--ui-img-filter-test")&&i+1<argc){app.test=true;app.testReport=args[++i];app.benchmark=arg==L"--ui-benchmark";app.featureTest=arg==L"--ui-feature-test";app.revisionTest=arg==L"--ui-revision-test";app.inputTest=arg==L"--ui-input-test";app.imgFilterTest=arg==L"--ui-img-filter-test";if(!app.featureTest&&!app.revisionTest&&!app.inputTest&&!app.imgFilterTest&&i+1<argc&&args[i+1][0]!=L'-'){app.badPath=args[++i];app.testStage=-1;}}else if(arg==L"--input-case"&&i+1<argc){app.inputSpace=std::wstring_view(args[++i])==L"space";}else if(arg==L"--profile"&&i+1<argc)app.protocol.profile=rep::clientProfile(args[++i]);else if(arg==L"--codepage"&&i+1<argc)app.protocol.codePage=rep::clientCodePage(args[++i]);else if(arg==L"--open"&&i+1<argc)app.openPath=args[++i];else if(arg==L"--client"&&i+1<argc){app.client=args[++i];clientArgument=true;}}LocalFree(args);
    if(!app.test){std::ifstream saved(app.root/L"runtime"/L"client.txt");std::string value;std::getline(saved,value);if(!clientArgument&&!value.empty()&&std::filesystem::is_directory(rep::wide(value)))app.client=rep::wide(value);std::ifstream locale(app.root/L"runtime"/L"language.txt");std::getline(locale,value);app.english=value=="en";std::ifstream layout(app.root/L"runtime"/L"layout.txt");int savedWidth;double savedFraction;if(layout>>savedWidth>>savedFraction){app.sidebarWidth=savedWidth;app.playlistFraction=std::clamp(savedFraction,.1,.9);}std::ifstream audio(app.root/L"runtime"/L"audio.txt");int volume,muted;if(audio>>volume>>muted){app.audioVolume=std::clamp(volume,0,100);app.audioMuted=muted!=0;}std::ifstream canvas(app.root/L"runtime"/L"canvas.txt");rep::CanvasSettings settings;int mode;if(canvas>>mode>>settings.factor>>settings.width>>settings.height>>settings.left>>settings.top>>settings.right>>settings.bottom){if(mode>=0&&mode<=2){settings.mode=rep::CanvasMode(mode);try{settings.resolve(800,600);app.canvasSettings=settings;}catch(const std::exception&){}}}}
    WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));cls.hIconSm=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_DEFAULTCOLOR));cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));cls.lpszClassName=L"NativeRepPlayer";RegisterClassExW(&cls);auto window=CreateWindowExW(0,cls.lpszClassName,L"REP Player",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1280,790,nullptr,nullptr,instance,&app);if(!window)throw rep::Error("cannot create player window");if(auto dwm=LoadLibraryW(L"dwmapi.dll")){using Attribute=HRESULT(WINAPI*)(HWND,DWORD,LPCVOID,DWORD);auto set=reinterpret_cast<Attribute>(GetProcAddress(dwm,"DwmSetWindowAttribute"));BOOL dark=TRUE;if(set)set(window,20,&dark,sizeof(dark));FreeLibrary(dwm);}ShowWindow(window,show);UpdateWindow(window);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE&&app.audioPopup.window&&(msg.hwnd==app.audioPopup.window||IsChild(app.audioPopup.window,msg.hwnd))){DestroyWindow(app.audioPopup.window);continue;}if((msg.message==WM_KEYDOWN||msg.message==WM_KEYUP)&&msg.wParam==VK_SPACE&&GetFocus()!=app.search&&(GetFocus()==window||IsChild(window,GetFocus()))){if(msg.message==WM_KEYDOWN&&!(msg.lParam&(LPARAM(1)<<30)))app.worker->togglePause();continue;}if(msg.message==WM_KEYDOWN&&(msg.wParam==VK_LEFT||msg.wParam==VK_RIGHT)&&(GetFocus()==app.canvas||GetFocus()==app.window)){app.worker->stepFrame(msg.wParam==VK_LEFT?-1:1);continue;}if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}CoUninitialize();return int(msg.wParam);
}catch(const std::exception& e){MessageBoxW(nullptr,rep::wide(e.what()).c_str(),L"REP Player",MB_OK|MB_ICONERROR);return 1;}}
