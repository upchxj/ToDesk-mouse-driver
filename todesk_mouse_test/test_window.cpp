#include "internal.h"
#include "todesk_mouse.h"
#include <string>

namespace {
constexpr wchar_t demo_class[]=L"ToDeskMouseTest.Demo.1";
constexpr DWORD window_style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
struct Demo {
    HINSTANCE module=nullptr;
    TdMouse mouse=nullptr;
    HWND status=nullptr;
    bool failed=false;
    ~Demo() { if (mouse) td_close(mouse); }
    void message(const wchar_t* text) { SetWindowTextW(status,text); }
    void diagnostic() {
        const char* text=td_diagnostic();
        const int count=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);
        if (count<=0) { message(L"无法转换诊断文本。"); return; }
        std::wstring value(static_cast<size_t>(count),L'\0');
        MultiByteToWideChar(CP_UTF8,0,text,-1,value.data(),count);
        message(value.c_str());
    }
    HWND child(HWND parent,const wchar_t* klass,const wchar_t* text,DWORD style,int id,int x,int y,int w,int h) {
        HWND c=CreateWindowExW(0,klass,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),module,nullptr);
        if (!c) failed=true;
        SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        return c;
    }
};

LRESULT CALLBACK window_proc(HWND window,UINT msg,WPARAM wp,LPARAM lp) {
    if (msg==WM_NCCREATE) {
        const auto cs=reinterpret_cast<const CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto d=reinterpret_cast<Demo*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (!d) return DefWindowProcW(window,msg,wp,lp);
    try {
        if (msg==WM_CREATE) {
            d->child(window,L"STATIC",L"ToDesk 本机鼠标测试",0,0,20,18,560,24);
            d->child(window,L"STATIC",L"先连接，再使用下方按钮。点击测试延迟 2 秒，可点“断开”取消。",0,0,20,48,600,22);
            d->child(window,L"BUTTON",L"连接 / 重新检测",BS_PUSHBUTTON|WS_TABSTOP,1,20,85,150,32);
            d->child(window,L"BUTTON",L"断开",BS_PUSHBUTTON|WS_TABSTOP,2,180,85,90,32);
            d->child(window,L"STATIC",L"步长（1～1000）",0,0,300,92,130,22);
            d->child(window,L"EDIT",L"20",WS_BORDER|ES_NUMBER|ES_AUTOHSCROLL|WS_TABSTOP,3,435,86,90,30);
            d->child(window,L"BUTTON",L"↑",BS_PUSHBUTTON|WS_TABSTOP,10,85,135,60,34);
            d->child(window,L"BUTTON",L"←",BS_PUSHBUTTON|WS_TABSTOP,11,20,173,60,34);
            d->child(window,L"BUTTON",L"→",BS_PUSHBUTTON|WS_TABSTOP,12,150,173,60,34);
            d->child(window,L"BUTTON",L"↓",BS_PUSHBUTTON|WS_TABSTOP,13,85,211,60,34);
            d->child(window,L"BUTTON",L"滚轮 ↑",BS_PUSHBUTTON|WS_TABSTOP,14,245,142,100,34);
            d->child(window,L"BUTTON",L"滚轮 ↓",BS_PUSHBUTTON|WS_TABSTOP,15,355,142,100,34);
            d->child(window,L"BUTTON",L"左键（2 秒后）",BS_PUSHBUTTON|WS_TABSTOP,16,245,188,140,34);
            d->child(window,L"BUTTON",L"右键（2 秒后）",BS_PUSHBUTTON|WS_TABSTOP,17,395,188,140,34);
            d->child(window,L"BUTTON",L"释放所有按键",BS_PUSHBUTTON|WS_TABSTOP,18,245,234,140,34);
            d->status=d->child(window,L"EDIT",L"请先连接。首次使用需安装随附的旧版 ToDesk VHID 驱动。",
                WS_BORDER|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,20,20,290,570,180);
            return d->failed?-1:0;
        }
        if (msg==WM_COMMAND) {
            const int id=LOWORD(wp);
            if (id==1 || id==2) {
                KillTimer(window,1); KillTimer(window,2);
                if (d->mouse) td_close(d->mouse);
                d->mouse=nullptr;
                if (id==1) { d->mouse=td_open(); d->diagnostic(); }
                else d->message(L"已断开。");
                return 0;
            }
            if (id<10 || id>18) return 0;
            if (!d->mouse) { d->message(L"尚未连接，请先点击“连接 / 重新检测”。"); return 0; }
            if (id==16 || id==17) {
                KillTimer(window,1); KillTimer(window,2);
                if (!SetTimer(window,id==16?1:2,2000,nullptr)) d->message(L"无法创建点击计时器。");
                else d->message(L"2 秒后点击光标所在位置；点击“断开”可取消。");
                return 0;
            }
            UINT step=0;
            if (id>=10 && id<=13) {
                BOOL valid=FALSE;
                step=GetDlgItemInt(window,3,&valid,FALSE);
                if (!valid || step<1 || step>1000) { d->message(L"步长必须是 1～1000。"); return 0; }
            }
            int ok=1;
            if (id==10) ok=td_move(d->mouse,0,-static_cast<int>(step));
            if (id==11) ok=td_move(d->mouse,-static_cast<int>(step),0);
            if (id==12) ok=td_move(d->mouse,static_cast<int>(step),0);
            if (id==13) ok=td_move(d->mouse,0,static_cast<int>(step));
            if (id==14) ok=td_wheel(d->mouse,1);
            if (id==15) ok=td_wheel(d->mouse,-1);
            if (id==18) { KillTimer(window,1); KillTimer(window,2); ok=td_release_all(d->mouse); }
            if (ok) d->message(L"发送成功。"); else d->diagnostic();
            return 0;
        }
        if (msg==WM_TIMER) {
            KillTimer(window,wp);
            if (d->mouse && (wp==1 || wp==2)) {
                if (td_click(d->mouse,wp==1?TD_LEFT:TD_RIGHT,30)) d->message(L"点击发送成功。");
                else d->diagnostic();
            }
            return 0;
        }
        if (msg==WM_DESTROY) {
            KillTimer(window,1); KillTimer(window,2);
            if (d->mouse) td_close(d->mouse);
            d->mouse=nullptr;
            return 0;
        }
    } catch (...) {
        d->failed=true;
        if (msg==WM_CREATE) return -1;
        PostMessageW(window,WM_CLOSE,0,0);
        return 0;
    }
    return DefWindowProcW(window,msg,wp,lp);
}
}

int test_window_run(TestLog& log,bool hidden) {
    Demo demo;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           demo_class,&demo.module)) { log.add("FAIL: demo module handle.\n"); return 4; }
    WNDCLASSW wc{};
    wc.lpfnWndProc=window_proc; wc.hInstance=demo.module; wc.lpszClassName=demo_class;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
    if (!RegisterClassW(&wc)) { log.add("FAIL: demo window class: %lu\n",GetLastError()); return 4; }
    RECT rect{0,0,610,490};
    AdjustWindowRect(&rect,window_style,FALSE);
    HWND window=CreateWindowW(demo_class,L"ToDesk 模拟鼠标测试",window_style,
        CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,demo.module,&demo);
    if (!window) {
        log.add("FAIL: create demo window: %lu\n",GetLastError());
        UnregisterClassW(demo_class,demo.module);
        return 4;
    }
    if (hidden) {
        DestroyWindow(window);
    } else {
        ShowWindow(window,SW_SHOWNORMAL);
        MSG msg{};
        while (IsWindow(window)) {
            const BOOL got=GetMessageW(&msg,nullptr,0,0);
            if (got<=0) { if (got<0) demo.failed=true; break; }
            if (!IsDialogMessageW(window,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        }
        if (IsWindow(window)) DestroyWindow(window);
    }
    if (!UnregisterClassW(demo_class,demo.module)) demo.failed=true;
    log.add("Demo window lifecycle: %s.\n",demo.failed?"FAIL":"PASS");
    return demo.failed?4:0;
}
