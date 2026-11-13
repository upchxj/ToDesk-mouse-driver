#include "internal.h"
#include "todesk_mouse_test.h"
#include "todesk_mouse.h"
#include "../todesk/protocol.h"
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <climits>
#include <cwchar>
#include <mutex>
#include <thread>
#include <vector>

namespace {
std::mutex test_mutex;
thread_local TestLog last_log;

struct Mouse {
    TdMouse h = nullptr;
    ~Mouse() { if (h) td_close(h); }
};

// Keep C++ exceptions within the test runner; Windows callbacks never throw.
int run(int (*fn)(TestLog&)) noexcept {
    last_log.text[0] = 0;
    try {
        std::unique_lock<std::mutex> lock(test_mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            last_log.add("BUSY: another test or demo window is running.\n");
            return 5;
        }
        TestLog output;
        int result = 4;
        std::thread worker([&] {
            try { result = fn(output); }
            catch (...) { output.add("FAIL: exception in test worker.\n"); result = 4; }
        });
        worker.join();
        last_log = output;
        return result;
    } catch (...) {
        last_log.add("FAIL: unable to start or join test worker.\n");
        return 4;
    }
}

int self_test(TestLog& log) {
    const td_protocol::Report expected{0x41,0x0B,0x05,0x11,0x81,0x7F,0xFF,0x01,0,0,0};
    if (td_protocol::relative(0x11,-127,127,-1,1) != expected) {
        log.add("FAIL: report encoding.\n"); return 1;
    }
    for (int d : {0,1,-1,127,-127,128,-128,520192,-520192}) {
        int left=d, sum=0, count=0;
        while (left) {
            const int step=td_protocol::step(left);
            if (step < -127 || step > 127 || !step) {
                log.add("FAIL: split step.\n"); return 1;
            }
            sum+=step; left-=step; ++count;
        }
        if (sum!=d || count>4096 || !td_protocol::valid_delta(d)) {
            log.add("FAIL: split sum/boundary.\n"); return 1;
        }
    }
    if (td_protocol::valid_delta(INT_MAX) || td_protocol::valid_delta(INT_MIN) ||
        td_protocol::valid_delta(520193) || td_protocol::valid_delta(-520193) ||
        td_move(nullptr,1,0) != 0 || td_error()!=TD_INVALID_ARGUMENT ||
        td_button_set(nullptr,1) != 0 || td_error()!=TD_INVALID_ARGUMENT) {
        log.add("FAIL: invalid argument rejection.\n"); return 1;
    }
    log.add("PASS: report bytes, signed encoding, split totals, boundary rejection, null handles.\n");
    return 0;
}

int probe(TestLog& log) {
    Mouse mouse;
    mouse.h=td_open();
    log.add("%s",td_diagnostic());
    if (!mouse.h) {
        log.add("FAIL: error=%u win32=%lu\n",td_error(),td_win32_error()); return 2;
    }
    if (td_output_report_length(mouse.h)!=11 || !td_is_open(mouse.h)) {
        log.add("FAIL: invalid open state.\n"); return 2;
    }
    log.add("PASS: opened legacy ToDesk mouse control interface. No input sent.\n");
    return 0;
}

bool legacy_path(const wchar_t* path) {
    wchar_t instance[MAX_DEVICE_ID_LEN]{};
    DEVPROPTYPE type=0;
    ULONG bytes=sizeof(instance);
    if (CM_Get_Device_Interface_PropertyW(path,&DEVPKEY_Device_InstanceId,&type,
        reinterpret_cast<PBYTE>(instance),&bytes,0)!=CR_SUCCESS) return false;
    DEVINST node=0;
    if (CM_Locate_DevNodeW(&node,instance,CM_LOCATE_DEVNODE_NORMAL)!=CR_SUCCESS) return false;
    for (int i=0;i<10;++i) {
        wchar_t ids[4096]{};
        bytes=sizeof(ids);
        if (CM_Get_DevNode_Registry_PropertyW(node,CM_DRP_HARDWAREID,nullptr,ids,&bytes,0)==CR_SUCCESS) {
            ids[4094]=ids[4095]=0;
            for (const wchar_t* p=ids;*p;p+=wcslen(p)+1)
                if (_wcsicmp(p,L"root\\TodeskVhid")==0) return true;
        }
        DEVINST parent=0;
        if (CM_Get_Parent(&parent,node,0)!=CR_SUCCESS) break;
        node=parent;
    }
    return false;
}

struct RawState {
    TestLog* log;
    int events=0;
    long long x=0, y=0;
    bool positive=false, negative=false, failed=false, unexpected=false;
};

LRESULT CALLBACK raw_window(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message==WM_NCCREATE) {
        const auto cs=reinterpret_cast<const CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto state=reinterpret_cast<RawState*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message==WM_INPUT && state) {
        try {
            UINT bytes=0;
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp),RID_INPUT,nullptr,&bytes,sizeof(RAWINPUTHEADER))==UINT(-1) ||
                bytes<sizeof(RAWINPUTHEADER)) {
                state->failed=true;
            } else {
                std::vector<BYTE> storage(bytes);
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp),RID_INPUT,storage.data(),&bytes,sizeof(RAWINPUTHEADER))!=bytes) {
                    state->failed=true;
                } else {
                    const auto raw=reinterpret_cast<const RAWINPUT*>(storage.data());
                    wchar_t name[2048]{};
                    UINT chars=2048;
                    if (raw->header.dwType==RIM_TYPEMOUSE && bytes>=sizeof(RAWINPUT) &&
                        GetRawInputDeviceInfoW(raw->header.hDevice,RIDI_DEVICENAME,name,&chars)!=UINT(-1) && legacy_path(name)) {
                        const auto& m=raw->data.mouse;
                        ++state->events; state->x+=m.lLastX; state->y+=m.lLastY;
                        state->positive |= m.lLastX==20 && m.lLastY==0;
                        state->negative |= m.lLastX==-20 && m.lLastY==0;
                        state->unexpected |= m.usButtonFlags!=0 || (m.usFlags & MOUSE_MOVE_ABSOLUTE)!=0;
                        state->log->add("ToDesk RAWINPUT: dx=%ld dy=%ld buttons=0x%04X\n",m.lLastX,m.lLastY,m.usButtonFlags);
                    }
                }
            }
        } catch (...) { state->failed=true; }
    }
    return DefWindowProcW(window,message,wp,lp);
}

void pump(DWORD ms) {
    const auto end=GetTickCount64()+ms;
    do {
        MSG msg{};
        while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        Sleep(5);
    } while (GetTickCount64()<end);
}

constexpr wchar_t raw_class[]=L"ToDeskMouseTest.RawInput.1";
struct RawSession {
    HINSTANCE module=nullptr;
    HWND window=nullptr;
    bool registered_class=false, registered_input=false;
    ~RawSession() {
        if (registered_input) {
            RAWINPUTDEVICE remove{1,2,RIDEV_REMOVE,nullptr};
            RegisterRawInputDevices(&remove,1,sizeof(remove));
        }
        if (window) DestroyWindow(window);
        if (registered_class) UnregisterClassW(raw_class,module);
    }
};

int smoke(TestLog& log) {
    // Mouse raw input registration is process-wide. Refuse to replace the host's.
    UINT count=0;
    if (GetRegisteredRawInputDevices(nullptr,&count,sizeof(RAWINPUTDEVICE))==UINT(-1)) {
        log.add("FAIL: query existing RawInput registration: %lu\n",GetLastError()); return 4;
    }
    if (count) {
        std::vector<RAWINPUTDEVICE> devices(count);
        const UINT actual=GetRegisteredRawInputDevices(devices.data(),&count,sizeof(RAWINPUTDEVICE));
        if (actual==UINT(-1)) { log.add("FAIL: enumerate RawInput registration.\n"); return 4; }
        for (UINT i=0;i<actual;++i) {
            if (devices[i].usUsagePage==1 && (devices[i].usUsage==2 || devices[i].usUsage==0)) {
                log.add("FAIL: host owns mouse RawInput registration; use scripts/run_tests.ps1 in a separate process.\n"); return 6;
            }
        }
    }
    Mouse mouse;
    mouse.h=td_open();
    log.add("%s",td_diagnostic());
    if (!mouse.h) { log.add("FAIL: device unavailable.\n"); return 2; }
    RawState state{&log};
    RawSession session;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            raw_class,&session.module)) { log.add("FAIL: module handle.\n"); return 4; }
    WNDCLASSW wc{};
    wc.hInstance=session.module; wc.lpfnWndProc=raw_window; wc.lpszClassName=raw_class;
    session.registered_class=RegisterClassW(&wc)!=0;
    if (!session.registered_class) { log.add("FAIL: register window class: %lu\n",GetLastError()); return 4; }
    session.window=CreateWindowW(raw_class,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,session.module,&state);
    RAWINPUTDEVICE rid{1,2,RIDEV_INPUTSINK,session.window};
    if (!session.window || !RegisterRawInputDevices(&rid,1,sizeof(rid))) {
        log.add("FAIL: register RawInput: %lu\n",GetLastError()); return 4;
    }
    session.registered_input=true;
    pump(100);
    state.events=0; state.x=state.y=0;
    state.positive=state.negative=state.unexpected=false;
    log.add("Moving +20, then -20 HID counts; no clicks.\n");
    const bool forward=td_move(mouse.h,20,0)!=0;
    if (!forward) log.add("%s",td_diagnostic());
    pump(200);
    const bool backward=forward && td_move(mouse.h,-20,0)!=0;
    if (forward && !backward) log.add("%s",td_diagnostic());
    pump(500);
    const bool pass=forward && backward && state.positive && state.negative &&
        !state.failed && !state.unexpected && state.x==0 && state.y==0;
    log.add("Raw input verification: %s, events=%d sum=(%lld,%lld)\n",pass?"PASS":"FAIL",state.events,state.x,state.y);
    if (!pass) log.add("Use an interactive desktop; stop simultaneous input from other ToDesk sessions.\n");
    return pass?0:3;
}

int show(TestLog& log) { return test_window_run(log,false); }
int gui_check(TestLog& log) {
    for (int i=0;i<2;++i) {
        const int result=test_window_run(log,true);
        if (result) return result;
    }
    log.add("PASS: demo window created/destroyed twice. No input sent.\n");
    return 0;
}
}

extern "C" TD_TEST_API int __cdecl td_test_self_test() { return run(self_test); }
extern "C" TD_TEST_API int __cdecl td_test_probe() { return run(probe); }
extern "C" TD_TEST_API int __cdecl td_test_smoke() { return run(smoke); }
extern "C" TD_TEST_API int __cdecl td_test_show_window() { return run(show); }
extern "C" TD_TEST_API int __cdecl td_test_gui_check() { return run(gui_check); }
extern "C" TD_TEST_API const char* __cdecl td_test_log() { return last_log.text; }

