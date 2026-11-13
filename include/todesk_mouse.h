#pragma once
#include <stdint.h>

#if defined(TD_MOUSE_BUILD)
#define TD_API __declspec(dllexport)
#else
#define TD_API __declspec(dllimport)
#endif
#define TD_CALL __cdecl

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TdMouseContext* TdMouse;
enum {
    TD_OK = 0, TD_NOT_FOUND = 1, TD_ACCESS_DENIED = 2,
    TD_INCOMPATIBLE = 3, TD_IO_ERROR = 4, TD_INVALID_ARGUMENT = 5,
    TD_OUT_OF_MEMORY = 6, TD_EX_UNSUPPORTED = 7
};
enum { TD_LEFT = 0, TD_RIGHT = 1, TD_MIDDLE = 2, TD_X1 = 3, TD_X2 = 4 };

/* Only the legacy ROOT\TodeskVhid HID protocol is implemented.
   No driver installation occurs in td_open. No SendInput fallback is used.
   Errors and UTF-8 diagnostics are per calling thread. Copy strings before
   the next operation. Serialize td_close with all users of the same handle. */
TD_API TdMouse TD_CALL td_open(void);
TD_API void TD_CALL td_close(TdMouse mouse);
TD_API int TD_CALL td_is_open(TdMouse mouse);
TD_API const char* TD_CALL td_version(void);
TD_API unsigned int TD_CALL td_error(void);
TD_API unsigned long TD_CALL td_win32_error(void);
TD_API const char* TD_CALL td_diagnostic(void);
TD_API int TD_CALL td_output_report_length(TdMouse mouse);

/* Relative HID counts; Windows pointer speed/acceleration may affect pixels.
   Values above 4096*127 in magnitude are rejected before any write.
   Wheel delta is HID wheel units (normally one notch per unit), not 120 units.
   Return 1 on success, 0 on error; an I/O failure may follow partial movement. */
TD_API int TD_CALL td_move(TdMouse mouse, int dx, int dy);
TD_API int TD_CALL td_wheel(TdMouse mouse, int delta);
TD_API int TD_CALL td_hwheel(TdMouse mouse, int delta);
TD_API int TD_CALL td_button_down(TdMouse mouse, int button);
TD_API int TD_CALL td_button_up(TdMouse mouse, int button);
TD_API int TD_CALL td_button_set(TdMouse mouse, unsigned int mask);
TD_API unsigned int TD_CALL td_button_get(TdMouse mouse);
TD_API int TD_CALL td_release_all(TdMouse mouse);
TD_API int TD_CALL td_click(TdMouse mouse, int button, unsigned int hold_ms);

#ifdef __cplusplus
}
#endif
