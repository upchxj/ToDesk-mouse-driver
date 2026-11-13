# ToDesk Mouse

[中文](README.md) | English

A Windows project for local mouse control through the legacy ToDesk VHID device. It provides relative movement, mouse buttons and wheel input through a C API DLL, with C/C++ headers, import libraries, a test application and an offline driver installer.

## Features

- Relative movement, vertical wheel and horizontal wheel.
- Press, release and click operations for left, right, middle and two extra buttons.
- x86 and x64 application builds in Debug and Release configurations.
- Per-thread error codes and UTF-8 diagnostics.
- Command-line self-tests, device probing, input verification and an interactive test window.

## Project layout

| Path | Contents |
| --- | --- |
| `todesk.sln` | Visual Studio solution |
| `todesk/` | Mouse DLL implementation and HID report encoding |
| `include/` | Public API and test application headers |
| `todesk_mouse_test/` | Command-line and GUI test sources |
| `bin/` | DLLs, import libraries and test applications by architecture and configuration |
| `driver_legacy/` | Legacy ToDesk VHID driver package and device management utility |
| `driver-install/` | Offline installer and installer source archive |

## Requirements and scope

The bundled installer targets Intel/AMD x64 Windows 10/11. Use the x86 DLL for a 32-bit application and the x64 DLL for a 64-bit application. The x86 application build does not imply support for 32-bit Windows drivers. The projects use MSVC v143, C++17 and Windows SDK 10.0.

The current version is `1.0.0 (ToDesk legacy VHID)`. Only an installed legacy `ROOT\TodeskVhid` device is supported; `ToDeskVhidEx`, keyboard input and absolute positioning are unsupported. The DLL does not install drivers or provide a `SendInput` fallback. Relative movement uses HID units, so pixel displacement may depend on system pointer settings.

This project wraps the legacy device interface and is not presented as an official ToDesk SDK.

## License

Original project source and documentation are licensed under the [MIT License](LICENSE), with copyright attributed to `todesk project contributors`.

Bundled third-party drivers, driver catalogs, DevCon and their copies inside the installer or archive are excluded from this project's MIT grant and remain subject to their respective rights holders' terms. Complete redistribution terms for these components were not found in this folder; this project's license does not grant their redistribution rights.
