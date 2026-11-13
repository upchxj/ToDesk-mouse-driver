# ToDesk Mouse

中文 | [English](README.en.md)

一个面向 Windows 的本机鼠标控制项目，通过旧版 ToDesk VHID 设备实现相对移动、鼠标按键和滚轮输入。项目提供 C 接口 DLL、C/C++ 头文件及导入库，并附带测试程序和离线驱动安装器。

## 功能

- 相对移动、垂直滚轮和水平滚轮。
- 左键、右键、中键和两个侧键的按下、释放及点击。
- x86 / x64 两种应用架构，提供 Debug 和 Release 版本。
- 线程局部错误码和 UTF-8 诊断信息。
- 命令行自检、设备探测、输入验证及可视化测试窗口。

## 项目结构

| 路径 | 内容 |
| --- | --- |
| `todesk.sln` | Visual Studio 解决方案 |
| `todesk/` | 鼠标 DLL 实现和 HID 报文编码 |
| `include/` | 公共接口与测试程序头文件 |
| `todesk_mouse_test/` | 命令行及窗口测试程序源码 |
| `bin/` | 各架构和配置的 DLL、导入库及测试程序 |
| `driver_legacy/` | 旧版 ToDesk VHID 驱动包和设备管理工具 |
| `driver-install/` | 离线安装器及安装器源码压缩包 |

## 环境与范围

随附安装器面向 Intel/AMD x64 Windows 10/11。32 位应用使用 x86 DLL，64 位应用使用 x64 DLL；x86 应用版本不表示驱动支持 32 位 Windows。工程采用 MSVC v143、C++17 和 Windows SDK 10.0。

当前版本为 `1.0.0 (ToDesk legacy VHID)`，仅支持已安装的旧版 `ROOT\TodeskVhid` 设备，不支持 `ToDeskVhidEx`、键盘输入或绝对坐标定位。DLL 本身不安装驱动，也不使用 `SendInput` 回退。相对移动采用 HID 单位，实际像素距离可能受到系统指针设置影响。

本项目是对旧版设备接口的封装，不代表 ToDesk 官方 SDK。

## 许可证

本项目原创源码和文档采用 [MIT License](LICENSE)，版权署名为 `todesk project contributors`。

随附的第三方驱动、驱动目录文件、DevCon 工具及其在安装器或压缩包中的副本不属于本项目 MIT 授权范围，继续受各自权利人的条款约束。当前目录未提供这些第三方组件的完整再分发授权文本，本项目许可证不授予其再分发权。
