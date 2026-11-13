#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "todesk_mouse_test.h"
#include <cstdio>
#include <cwchar>

namespace {
int report(int (*test)()) {
    const int result=test();
    std::fputs(td_test_log(),stdout);
    std::fflush(stdout);
    return result;
}

int show_window() {
    // Hide only a new console owned by this process, not the user's terminal.
    DWORD processes[2]{};
    const bool own_console=GetConsoleProcessList(processes,2)==1 && processes[0]==GetCurrentProcessId();
    HWND console=own_console?GetConsoleWindow():nullptr;
    if (console) ShowWindow(console,SW_HIDE);
    const int result=td_test_show_window();
    if (result) MessageBoxA(nullptr,td_test_log(),"ToDesk mouse test",MB_OK|MB_ICONERROR);
    return result;
}
}

int wmain(int argc,wchar_t** argv) {
    if (argc==1 || (argc==2 && wcscmp(argv[1],L"--gui")==0)) return show_window();
    if (argc!=2) {
        std::fputs("Usage: mouse_test.exe [--gui | --self-test | --probe | --smoke | --gui-check | --all]\n",stderr);
        return 64;
    }
    const wchar_t* command=argv[1];
    if (wcscmp(command,L"--self-test")==0) return report(td_test_self_test);
    if (wcscmp(command,L"--probe")==0) return report(td_test_probe);
    if (wcscmp(command,L"--smoke")==0) return report(td_test_smoke);
    if (wcscmp(command,L"--gui-check")==0) return report(td_test_gui_check);
    if (wcscmp(command,L"--all")==0) {
        int (*tests[])()={td_test_self_test,td_test_probe,td_test_gui_check,td_test_smoke};
        for (int round=1;round<=2;++round) {
            std::printf("Round %d/2\n",round);
            for (auto test:tests) {
                const int result=report(test);
                if (result) return result;
            }
        }
        return 0;
    }
    if (wcscmp(command,L"--help")==0) {
        std::puts("Usage: mouse_test.exe [--gui | --self-test | --probe | --smoke | --gui-check | --all]\n"
                  "No arguments: open test window. --all: run all checks twice, including +/-20 movement.");
        return 0;
    }
    std::fputs("Unknown argument. Use --help.\n",stderr);
    return 64;
}
