#pragma once

// Internal functions linked into mouse_test.exe, not a separate DLL API.
#define TD_TEST_API

#ifdef __cplusplus
extern "C" {
#endif

/* All tests return 0 on success. Other results:
   1=self-test failed, 2=device unavailable, 3=input verification failed,
   4=Windows/test-host error, 5=another test is running,
   6=host already owns mouse RawInput registration.
   Functions block until complete.
   Tests are serialized per process and own a temporary worker thread.
   Call td_test_log on the same calling thread, before its next test call. */
TD_TEST_API int __cdecl td_test_self_test(void);
TD_TEST_API int __cdecl td_test_probe(void);
/* Sends +20/-20 relative counts, without clicks or wheel events.
   Requires the interactive desktop and no existing mouse RawInput registration.
   Execute mouse_test.exe in a separate process if your app owns RawInput. */
TD_TEST_API int __cdecl td_test_smoke(void);
/* Opens the interactive demo and returns after the window closes. */
TD_TEST_API int __cdecl td_test_show_window(void);
/* Creates and destroys a hidden demo window twice, without input. */
TD_TEST_API int __cdecl td_test_gui_check(void);
TD_TEST_API const char* __cdecl td_test_log(void);

#ifdef __cplusplus
}
#endif
