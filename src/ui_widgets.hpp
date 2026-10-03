#pragma once
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <functional>

namespace rep::ui {
// Native list/tree controls remain responsible for selection, keyboard input and
// wheel scrolling. Their non-client scrollbar is outside this clipping parent.
inline LRESULT CALLBACK clippedPaneProcedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_NOTIFY||message==WM_COMMAND)return SendMessageW(GetParent(window),message,wp,lp);
    if(message==WM_ERASEBKGND){RECT r;GetClientRect(window,&r);SetDCBrushColor(reinterpret_cast<HDC>(wp),RGB(41,42,45));FillRect(reinterpret_cast<HDC>(wp),&r,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));return 1;}
    return DefWindowProcW(window,message,wp,lp);
}
inline HWND createClippedPane(HWND parent,int id){
    WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.lpfnWndProc=clippedPaneProcedure;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"RepClippedPane";RegisterClassExW(&cls);
    return CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,1,1,parent,reinterpret_cast<HMENU>(INT_PTR(id)),cls.hInstance,nullptr);
}
class NativeScrollbar {
    bool dragging_=false;int dragOffset_=0;
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
        auto* bar=reinterpret_cast<NativeScrollbar*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){bar=static_cast<NativeScrollbar*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);bar->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(bar));}
        if(!bar)return DefWindowProcW(window,message,wp,lp);
        switch(message){
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT paint;auto dc=BeginPaint(window,&paint);RECT track;GetClientRect(window,&track);SetDCBrushColor(dc,RGB(41,42,45));FillRect(dc,&track,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));auto thumb=bar->thumbRect();if(bar->range().maximum>0){auto oldPen=SelectObject(dc,GetStockObject(NULL_PEN));SetDCBrushColor(dc,bar->dragging_?RGB(143,148,158):RGB(85,90,99));auto oldBrush=SelectObject(dc,GetStockObject(DC_BRUSH));RoundRect(dc,thumb.left,thumb.top,thumb.right,thumb.bottom,6,6);SelectObject(dc,oldBrush);SelectObject(dc,oldPen);}EndPaint(window,&paint);return 0;}
        case WM_LBUTTONDOWN:{SetFocus(bar->target);auto r=bar->thumbRect();POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(PtInRect(&r,point)){bar->dragging_=true;bar->dragOffset_=point.y-r.top;SetCapture(window);}else{auto state=bar->range();bar->scrollTo(state.position+(point.y<r.top?-state.page:state.page));}InvalidateRect(window,nullptr,FALSE);return 0;}
        case WM_MOUSEMOVE:if(bar->dragging_){RECT r;GetClientRect(window,&r);auto thumb=bar->thumbRect();int travel=std::max(1,int(r.bottom)-4-int(thumb.bottom-thumb.top));int y=std::clamp(GET_Y_LPARAM(lp)-bar->dragOffset_-2,0,travel);bar->scrollTo(int((int64_t(y)*bar->range().maximum+travel/2)/travel));}return 0;
        case WM_LBUTTONUP:if(bar->dragging_){bar->dragging_=false;ReleaseCapture();InvalidateRect(window,nullptr,FALSE);}return 0;
        case WM_CAPTURECHANGED:bar->dragging_=false;InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_MOUSEWHEEL:SendMessageW(bar->target,message,wp,lp);InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_KEYDOWN:SendMessageW(bar->target,message,wp,lp);InvalidateRect(window,nullptr,FALSE);return 0;
        }
        return DefWindowProcW(window,message,wp,lp);
    }
public:
    struct Range {int total=0,page=1,position=0,maximum=0;};
    HWND window=nullptr,target=nullptr;bool tree=false;
    void create(HWND parent,HWND control,bool isTree,int id){
        target=control;tree=isTree;WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"RepDarkScrollbar";RegisterClassExW(&cls);
        window=CreateWindowExW(0,cls.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,0,0,8,1,parent,reinterpret_cast<HMENU>(INT_PTR(id)),cls.hInstance,this);
    }
    Range range()const{
        Range result;RECT client;GetClientRect(GetParent(target),&client);
        if(tree){auto first=TreeView_GetFirstVisible(target);for(auto item=TreeView_GetRoot(target);item;item=TreeView_GetNextVisible(target,item)){if(item==first)result.position=result.total;result.total++;}result.page=std::max(1,int(client.bottom)/std::max(1,int(TreeView_GetItemHeight(target))));}
        else{result.total=ListView_GetItemCount(target);RECT row{};ListView_GetItemRect(target,0,&row,LVIR_BOUNDS);result.page=std::max(1,int(client.bottom)/std::max(1,int(row.bottom-row.top)));result.position=std::max(0,ListView_GetTopIndex(target));}
        result.maximum=std::max(0,result.total-result.page);result.position=std::clamp(result.position,0,result.maximum);return result;
    }
    RECT thumbRect()const{
        RECT client;GetClientRect(window,&client);auto state=range();int height=std::max(1,int(client.bottom)-4),thumb=state.total?std::clamp(int(int64_t(height)*state.page/std::max(1,state.total)),std::min(24,height),height):height;int travel=height-thumb,top=2+(state.maximum?int(int64_t(travel)*state.position/state.maximum):0);return RECT{0,top,client.right,top+thumb};
    }
    void scrollTo(int position){
        auto state=range();position=std::clamp(position,0,state.maximum);
        if(tree){auto item=TreeView_GetRoot(target);for(int i=0;item&&i<position;i++)item=TreeView_GetNextVisible(target,item);if(item)SendMessageW(target,TVM_SELECTITEM,TVGN_FIRSTVISIBLE,reinterpret_cast<LPARAM>(item));}
        else{RECT row{};ListView_GetItemRect(target,0,&row,LVIR_BOUNDS);int rowHeight=std::max(1,int(row.bottom-row.top));ListView_Scroll(target,0,(position-ListView_GetTopIndex(target))*rowHeight);}
        InvalidateRect(window,nullptr,FALSE);
    }
    void refresh(){if(window)InvalidateRect(window,nullptr,FALSE);}
};
class PaneSplitter {
    bool dragging_=false;
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
        auto* splitter=reinterpret_cast<PaneSplitter*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){splitter=static_cast<PaneSplitter*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);splitter->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(splitter));}if(!splitter)return DefWindowProcW(window,message,wp,lp);
        switch(message){
        case WM_SETCURSOR:SetCursor(LoadCursorW(nullptr,splitter->vertical?IDC_SIZEWE:IDC_SIZENS));return TRUE;
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT p;auto dc=BeginPaint(window,&p);RECT r;GetClientRect(window,&r);SetDCBrushColor(dc,splitter->dragging_?RGB(139,126,72):RGB(24,25,27));FillRect(dc,&r,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));EndPaint(window,&p);return 0;}
        case WM_LBUTTONDOWN:splitter->dragging_=true;SetCapture(window);InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_MOUSEMOVE:if(splitter->dragging_&&splitter->onDrag){POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};MapWindowPoints(window,GetParent(window),&point,1);splitter->onDrag(splitter->vertical?point.x:point.y);}return 0;
        case WM_LBUTTONUP:if(splitter->dragging_){splitter->dragging_=false;ReleaseCapture();if(splitter->onDone)splitter->onDone();InvalidateRect(window,nullptr,FALSE);}return 0;
        case WM_CAPTURECHANGED:splitter->dragging_=false;InvalidateRect(window,nullptr,FALSE);return 0;
        }
        return DefWindowProcW(window,message,wp,lp);
    }
public:
    HWND window=nullptr;bool vertical=true;std::function<void(int)> onDrag;std::function<void()> onDone;
    void create(HWND parent,bool isVertical,int id,std::function<void(int)> drag,std::function<void()> done){vertical=isVertical;onDrag=std::move(drag);onDone=std::move(done);WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"RepPaneSplitter";RegisterClassExW(&cls);window=CreateWindowExW(0,cls.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,0,0,6,6,parent,reinterpret_cast<HMENU>(INT_PTR(id)),cls.hInstance,this);}
};
}
