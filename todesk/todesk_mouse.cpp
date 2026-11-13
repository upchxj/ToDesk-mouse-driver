#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define TD_MOUSE_BUILD
#include <Windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <hidsdi.h>
#include <hidpi.h>
#include "todesk_mouse.h"
#include "protocol.h"
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "hid.lib")

namespace {
thread_local unsigned int last_error = TD_OK;
thread_local DWORD last_win32 = ERROR_SUCCESS;
thread_local char diagnostic[16384] = {};

void reset_error() {
    last_error = TD_OK;
    last_win32 = ERROR_SUCCESS;
    diagnostic[0] = 0;
}
void note(const char* text) {
    const size_t n = strlen(diagnostic);
    if (n < sizeof(diagnostic) - 1)
        strncpy_s(diagnostic + n, sizeof(diagnostic) - n, text, _TRUNCATE);
}
int fail(unsigned int code, DWORD win32, const char* text) {
    last_error = code;
    last_win32 = win32;
    note(text);
    return 0;
}
void note_path(const wchar_t* path) {
    char buf[2048] = {};
    if (WideCharToMultiByte(CP_UTF8, 0, path, -1, buf, sizeof(buf), nullptr, nullptr)) {
        note(buf);
        note("\n");
    }
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct DeviceList {
    HDEVINFO value = INVALID_HANDLE_VALUE;
    ~DeviceList() { if (value != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value); }
};
struct Preparsed {
    PHIDP_PREPARSED_DATA value = nullptr;
    ~Preparsed() { if (value) HidD_FreePreparsedData(value); }
};

// Match complete device ID tokens, never VID/PID alone or arbitrary HID devices.
bool token(const wchar_t* id, const wchar_t* name) {
    const size_t n = wcslen(name);
    for (const wchar_t* p = id; *p; ++p) {
        if (p != id && p[-1] != L'\\' && p[-1] != L'&' && p[-1] != L'#') continue;
        if (_wcsnicmp(p, name, n) == 0 &&
            (p[n] == 0 || p[n] == L'\\' || p[n] == L'&' || p[n] == L'#')) return true;
    }
    return false;
}
enum Identity { Other, Legacy, Ex };
Identity identity(DEVINST node) {
    Identity result = Other;
    for (int depth = 0; depth < 10; ++depth) {
        wchar_t id[MAX_DEVICE_ID_LEN] = {};
        if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
            if (token(id, L"ToDeskVhidEx")) return Ex;
            if (token(id, L"TodeskVhid")) result = Legacy;
        }
        wchar_t ids[4096] = {};
        ULONG bytes = sizeof(ids);
        if (CM_Get_DevNode_Registry_PropertyW(node, CM_DRP_HARDWAREID, nullptr,
                                             ids, &bytes, 0) == CR_SUCCESS) {
            ids[4094] = ids[4095] = 0;
            for (const wchar_t* p = ids; *p; p += wcslen(p) + 1) {
                if (token(p, L"ToDeskVhidEx")) return Ex;
                if (token(p, L"TodeskVhid")) result = Legacy;
            }
        }
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
        node = parent;
    }
    return result;
}

bool compatible(HANDLE handle) {
    HIDD_ATTRIBUTES attr{};
    attr.Size = sizeof(attr);
    if (!HidD_GetAttributes(handle, &attr)) return false;
    Preparsed p;
    if (!HidD_GetPreparsedData(handle, &p.value)) return false;
    HIDP_CAPS caps{};
    if (HidP_GetCaps(p.value, &caps) != HIDP_STATUS_SUCCESS) return false;
    char line[256];
    sprintf_s(line, "VID=%04X PID=%04X page=%04X usage=%04X input=%u output=%u feature=%u\n",
              attr.VendorID, attr.ProductID, caps.UsagePage, caps.Usage,
              caps.InputReportByteLength, caps.OutputReportByteLength, caps.FeatureReportByteLength);
    note(line);
    if (attr.VendorID != 0x2202 || attr.ProductID != 0x4432 ||
        caps.UsagePage != 0xFF00 || caps.Usage != 1 ||
        caps.OutputReportByteLength != td_protocol::kReportBytes ||
        caps.InputReportByteLength != 11 || caps.FeatureReportByteLength != 10) return false;
    USHORT count = caps.NumberOutputValueCaps;
    sprintf_s(line, "Output value caps=%u button caps=%u data indices=%u\n",
              count, caps.NumberOutputButtonCaps, caps.NumberOutputDataIndices);
    note(line);
    // The original descriptor declares an Array (91 00), so Windows exposes
    // its control report through ButtonCaps even though its elements are bytes.
    if (caps.NumberOutputButtonCaps == 1 && count == 0) {
        HIDP_BUTTON_CAPS button{};
        USHORT buttons = 1;
        if (HidP_GetButtonCaps(HidP_Output, &button, &buttons, p.value) == HIDP_STATUS_SUCCESS &&
            buttons == 1 && button.ReportID == 0x41 && button.UsagePage == 0xFF00 &&
            !button.IsRange && button.NotRange.Usage == 1 && button.BitField == 0) return true;
    }
    return false;
}
}

struct TdMouseContext {
    HANDLE handle = INVALID_HANDLE_VALUE;
    HANDLE event = nullptr;
    uint8_t buttons = 0;
    bool uncertain = false;
    std::mutex mutex;
    ~TdMouseContext() {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        if (event) CloseHandle(event);
    }

    bool send(uint8_t next, int dx, int dy, int wheel, int hwheel) {
        const auto report = td_protocol::relative(next, dx, dy, wheel, hwheel);
        OVERLAPPED overlapped{};
        overlapped.hEvent = event;
        ResetEvent(event);
        DWORD written = 0;
        BOOL ok = WriteFile(handle, report.data(), static_cast<DWORD>(report.size()), &written, &overlapped);
        DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        bool timed_out = false;
        if (!ok && error == ERROR_IO_PENDING) {
            const DWORD wait = WaitForSingleObject(event, 1000);
            if (wait != WAIT_OBJECT_0) {
                timed_out = wait == WAIT_TIMEOUT;
                const DWORD wait_error = timed_out ? ERROR_TIMEOUT : GetLastError();
                CancelIoEx(handle, &overlapped);
                // Drain before stack-backed OVERLAPPED/report go out of scope.
                ok = GetOverlappedResult(handle, &overlapped, &written, TRUE);
                error = ok ? ERROR_SUCCESS : wait_error;
            } else {
                ok = GetOverlappedResult(handle, &overlapped, &written, FALSE);
                error = ok ? ERROR_SUCCESS : GetLastError();
            }
        }
        if (!ok || written != report.size()) {
            uncertain = true;
            char line[256];
            sprintf_s(line, "WriteFile failed: win32=%lu, bytes=%lu/11%s. Movement may be partial.\n",
                      error ? error : ERROR_WRITE_FAULT, written, timed_out ? ", timeout" : "");
            fail(TD_IO_ERROR, error ? error : ERROR_WRITE_FAULT, line);
            return false;
        }
        buttons = next;
        uncertain = false;
        return true;
    }
};

extern "C" TD_API TdMouse TD_CALL td_open() {
    reset_error();
    note("todesk_mouse 1.0.0; backend=ToDesk legacy VHID; transport=WriteFile\n");
    try {
        GUID guid{};
        HidD_GetHidGuid(&guid);
        DeviceList list;
        list.value = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (list.value == INVALID_HANDLE_VALUE) {
            fail(TD_IO_ERROR, GetLastError(), "HID enumeration failed.\n");
            return nullptr;
        }
        bool found_legacy = false, found_ex = false, found_compatible = false;
        DWORD open_error = ERROR_SUCCESS;
        for (DWORD index = 0;; ++index) {
            SP_DEVICE_INTERFACE_DATA iface{};
            iface.cbSize = sizeof(iface);
            if (!SetupDiEnumDeviceInterfaces(list.value, nullptr, &guid, index, &iface)) {
                const DWORD error = GetLastError();
                if (error != ERROR_NO_MORE_ITEMS) {
                    fail(TD_IO_ERROR, error, "HID interface enumeration stopped unexpectedly.\n");
                    return nullptr;
                }
                break;
            }
            DWORD size = 0;
            SetupDiGetDeviceInterfaceDetailW(list.value, &iface, nullptr, 0, &size, nullptr);
            if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || size > 65536) continue;
            std::vector<BYTE> storage(size);
            auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
            detail->cbSize = sizeof(*detail);
            SP_DEVINFO_DATA device{};
            device.cbSize = sizeof(device);
            if (!SetupDiGetDeviceInterfaceDetailW(list.value, &iface, detail, size, nullptr, &device)) continue;
            const Identity kind = identity(device.DevInst);
            if (kind == Other) continue;
            if (kind == Ex) {
                found_ex = true;
                continue;
            }
            found_legacy = true;
            note_path(detail->DevicePath);
            Handle query;
            query.value = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, 0, nullptr);
            if (query.value == INVALID_HANDLE_VALUE) {
                open_error = GetLastError();
                continue;
            }
            if (!compatible(query.value)) continue;
            found_compatible = true;
            for (DWORD access : {DWORD(GENERIC_READ | GENERIC_WRITE), DWORD(GENERIC_WRITE)}) {
                Handle writer;
                writer.value = CreateFileW(detail->DevicePath, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                           nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
                if (writer.value == INVALID_HANDLE_VALUE) {
                    open_error = GetLastError();
                    continue;
                }
                auto ctx = new TdMouseContext;
                ctx->handle = writer.value;
                writer.value = INVALID_HANDLE_VALUE;
                ctx->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (!ctx->event) {
                    const DWORD error = GetLastError();
                    delete ctx;
                    fail(TD_IO_ERROR, error, "CreateEvent failed.\n");
                    return nullptr;
                }
                note("Opened verified legacy control collection, report=0x41, bytes=11.\n");
                return ctx;
            }
        }
        if (open_error) fail(open_error == ERROR_ACCESS_DENIED ? TD_ACCESS_DENIED : TD_IO_ERROR,
                             open_error, "ToDesk device open/query failed; inspect Win32 error.\n");
        else if (found_compatible) fail(TD_IO_ERROR, 0, "Compatible control port could not be opened.\n");
        else if (found_legacy) fail(TD_INCOMPATIBLE, 0, "ToDesk legacy device found but descriptor does not match this implementation.\n");
        else if (found_ex) fail(TD_EX_UNSUPPORTED, ERROR_NOT_SUPPORTED,
            "Only ToDeskVhidEx was found. Its RPC protocol is not implemented; legacy reports are not sent to it.\n");
        else fail(TD_NOT_FOUND, ERROR_NOT_FOUND,
            "No ROOT\\TodeskVhid device found. Having DLL/SYS files on disk does not install a PnP device.\n");
        return nullptr;
    } catch (const std::bad_alloc&) {
        fail(TD_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY, "Allocation failed.\n");
    } catch (...) {
        fail(TD_IO_ERROR, ERROR_GEN_FAILURE, "Unexpected open failure.\n");
    }
    return nullptr;
}

namespace {
template<class F> int invoke(TdMouse h, F action) {
    reset_error();
    if (!h) return fail(TD_INVALID_ARGUMENT, ERROR_INVALID_HANDLE, "Null mouse handle.\n");
    try {
        std::lock_guard<std::mutex> lock(h->mutex);
        return action();
    } catch (...) {
        return fail(TD_IO_ERROR, ERROR_GEN_FAILURE, "Unexpected operation failure.\n");
    }
}
bool valid_button(int button) { return button >= TD_LEFT && button <= TD_X2; }
int movement(TdMouse h, int x, int y, int w, int hw) {
    return invoke(h, [&] {
        if (!td_protocol::valid_delta(x) || !td_protocol::valid_delta(y) ||
            !td_protocol::valid_delta(w) || !td_protocol::valid_delta(hw))
            return fail(TD_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER, "Delta exceeds 520192; no report sent.\n");
        while (x || y || w || hw) {
            const int sx = td_protocol::step(x), sy = td_protocol::step(y);
            const int sw = td_protocol::step(w), sh = td_protocol::step(hw);
            if (!h->send(h->buttons, sx, sy, sw, sh)) return 0;
            x -= sx; y -= sy; w -= sw; hw -= sh;
        }
        return 1;
    });
}
}

extern "C" TD_API void TD_CALL td_close(TdMouse h) {
    if (!h) return;
    invoke(h, [&] { return (h->buttons || h->uncertain) ? int(h->send(0,0,0,0,0)) : 1; });
    delete h;
}
extern "C" TD_API int TD_CALL td_is_open(TdMouse h) { return h != nullptr; }
extern "C" TD_API const char* TD_CALL td_version() { return "1.0.0 (ToDesk legacy VHID)"; }
extern "C" TD_API unsigned int TD_CALL td_error() { return last_error; }
extern "C" TD_API unsigned long TD_CALL td_win32_error() { return last_win32; }
extern "C" TD_API const char* TD_CALL td_diagnostic() { return diagnostic; }
extern "C" TD_API int TD_CALL td_output_report_length(TdMouse h) { return h ? td_protocol::kReportBytes : 0; }
extern "C" TD_API int TD_CALL td_move(TdMouse h,int x,int y) { return movement(h,x,y,0,0); }
extern "C" TD_API int TD_CALL td_wheel(TdMouse h,int d) { return movement(h,0,0,d,0); }
extern "C" TD_API int TD_CALL td_hwheel(TdMouse h,int d) { return movement(h,0,0,0,d); }
extern "C" TD_API int TD_CALL td_button_set(TdMouse h,unsigned int mask) {
    return invoke(h, [&] {
        if (mask > 0x1F) return fail(TD_INVALID_ARGUMENT,ERROR_INVALID_PARAMETER,"Button mask must be 0..31.\n");
        return int(h->send(static_cast<uint8_t>(mask),0,0,0,0));
    });
}
extern "C" TD_API int TD_CALL td_button_down(TdMouse h,int b) {
    return invoke(h, [&] {
        if (!valid_button(b)) return fail(TD_INVALID_ARGUMENT,ERROR_INVALID_PARAMETER,"Button must be 0..4.\n");
        return int(h->send(static_cast<uint8_t>(h->buttons | (1u<<b)),0,0,0,0));
    });
}
extern "C" TD_API int TD_CALL td_button_up(TdMouse h,int b) {
    return invoke(h, [&] {
        if (!valid_button(b)) return fail(TD_INVALID_ARGUMENT,ERROR_INVALID_PARAMETER,"Button must be 0..4.\n");
        return int(h->send(static_cast<uint8_t>(h->buttons & ~(1u<<b)),0,0,0,0));
    });
}
extern "C" TD_API unsigned int TD_CALL td_button_get(TdMouse h) {
    unsigned int value = 0;
    invoke(h, [&] { value = h->buttons; return 1; });
    return value;
}
extern "C" TD_API int TD_CALL td_release_all(TdMouse h) { return td_button_set(h,0); }
extern "C" TD_API int TD_CALL td_click(TdMouse h,int b,unsigned int ms) {
    return invoke(h, [&] {
        if (!valid_button(b) || ms > 10000) return fail(TD_INVALID_ARGUMENT,ERROR_INVALID_PARAMETER,"Invalid button or hold_ms > 10000.\n");
        const uint8_t bit = static_cast<uint8_t>(1u<<b);
        if (h->buttons & bit) return fail(TD_INVALID_ARGUMENT,ERROR_INVALID_STATE,"Button already held; release it first.\n");
        const uint8_t original = h->buttons;
        if (!h->send(static_cast<uint8_t>(original | bit),0,0,0,0)) return 0;
        Sleep(ms ? ms : 30);
        return int(h->send(original,0,0,0,0));
    });
}
