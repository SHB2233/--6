# 实验6 Socket通信编程

本项目包含基础 TCP 一对一文字通信和 Qt 图形聊天室。聊天室支持昵称登录、在线列表、公共大厅、创建/加入群组、群内广播、私聊、群内文件发送，以及经服务器发起呼叫后建立的客户端直接 TCP 文字会话。

基础程序默认端口为 **8888**，聊天室默认端口为 **8889**。直接文字会话由被叫临时监听 IPv4 端口，主叫连接该端口；服务器只转发呼叫信令。登录使用昵称，没有账户密码认证；群组、成员和呼叫状态仅保存在服务器内存中，重启后重置，没有离线消息或历史记录。

## 直接运行 Windows 交付包

将完整 `delivery/runtime/` 文件夹复制到本地可写目录，不要只复制 EXE。可双击 `start_server.cmd` 启动图形服务器、双击 `start_client.cmd` 启动客户端，也可在该目录运行：

```powershell
.\QtTcpChat.exe --server --port 8889
.\QtTcpChat.exe --connect --host 127.0.0.1 --port 8889 --nickname Alice
.\QtTcpChat.exe --connect --host 127.0.0.1 --port 8889 --nickname Bob
.\QtTcpChat.exe --connect --host 127.0.0.1 --port 8889 --nickname Charlie
```

每条命令对应一个独立进程。单机演示填 `127.0.0.1`；局域网演示填服务器 IPv4，并按本机策略允许聊天室端口和被叫临时 TCP 监听端口。当前证据来自同机 loopback，不代表已经完成双机或 NAT 环境验证。项目没有语音、视频或 NAT 穿透功能。

昵称去除两端空白后必须为 1～24 个 UTF-16 代码单元，不能包含内部空白，不能与在线昵称重复（不区分大小写）。普通文字发往当前已加入群组；私聊输入 `/w Bob 你好`。选择尚未加入的群后，先点击“加入群组”。发送文件时使用当前群，其他群成员可保存或取消；单文件上限为 **5 MiB = 5,242,880 字节**。

在右侧选择其他在线用户后点击“呼叫选中用户”。对方接听后出现独立 TCP 文字窗口；未接听 30 秒结束，接听后建连和关联限时 10 秒。关闭文字窗口或点击“取消／结束呼叫”结束呼叫，聊天室连接继续可用。

基础程序需开两个终端，先启动服务器，再启动客户端。客户端先发送一句，服务器输入一句回复；输入 `/quit` 结束：

```powershell
.\tcp_basic_server.exe 8888
.\tcp_basic_client.exe 127.0.0.1 8888
```

## 构建

已验证环境：Windows x64、Qt 5.15.2 的 Core/Gui/Widgets/Network/Test 模块、MSVC 19.50、C++17。QtTest 显示的 `windows 10` 是内核版本，本文不据此推断 Windows 发行名称。Qt 6 未测试，当前 `QTextStream::setCodec` 等接口按 Qt 5 编写。

构建脚本使用 PowerShell 7。请加载与 Qt 安装匹配的 MSVC x64 开发环境，并让 `qmake.exe` 所在目录在 PATH 中。以下命令在本仓库目录执行，路径由本机开发环境解析：

```powershell
$qtBin = Split-Path (Get-Command qmake.exe).Source
$vcvars = Join-Path $env:VSINSTALLDIR 'VC\Auxiliary\Build\vcvars64.bat'
.\run_build.ps1 -QtBin $qtBin -VcVars $vcvars
```

也可显式传入本机 Qt 和 `vcvars64.bat` 的路径。脚本在仓库父目录 `build/` 下分别构建主程序、基础程序和三个 QtTest 工程，保存各工程构建日志。不要混用 MSVC 版 Qt 与 MinGW 编译器。使用 conda Qt 时，其 `_conda` DLL 和导入库需要与当前 Qt 安装保持一致。

## 回归测试和界面截图

已有 Python 3 即可运行网络测试；这些测试只依赖 Python 标准库。QtTest 使用构建环境的 Qt DLL。先构建，再执行：

```powershell
.\run_tests.ps1 -SkipBuild -Python python
```

脚本运行基础 TCP、真实 Qt 服务器协议、DirectCall、服务器对象及三个 Qt UI 场景，日志保存在父目录 `delivery/evidence/`。UI 测试实际点击窗口完成群聊、私聊、群组隔离、二进制/空文件和直接文字会话，并生成六张窗口截图。`ui_test` 中的四个窗口属于一个测试进程；独立 EXE 进程的验证见后面的运行包检查。

截至 2026-10-13，证据日志对应 9 个基础测试、16 个聊天室协议测试、10 个 DirectCall 测试、1 个服务器对象测试和 3 个 UI 场景，共 **39 个行为测试**。QtTest 日志中的 initTestCase/cleanupTestCase 不计入行为测试；重新运行后的结论以日志为准。报告脚本从日志读取计数。

## 打包和脱离开发 PATH 的检查

在 MSVC x64 Developer PowerShell 中运行：

```powershell
.\run_package.ps1 -QtBin $qtBin -Dumpbin (Get-Command dumpbin.exe).Source `
  -Redist (Join-Path $env:VSINSTALLDIR 'VC\Redist\MSVC') -Python python
python -X utf8 .\scripts\verify_runtime.py
```

打包脚本调用 `windeployqt`，再根据 `dumpbin /DEPENDENTS` 补齐当前 conda Qt 所需的非系统 DLL、MSVC CRT 和平台插件。输出位于父目录 `delivery/runtime/`，不写入源码仓库。

`verify_runtime.py` 去掉 Qt/conda 开发环境变量，将 PATH 限定为运行包和 Windows 系统目录；运行包中的基础/聊天室测试成功后，再启动一个图形服务器和三个图形客户端，检查在线成员及 Qt DLL 加载来源。当前 `runtime_verification.json` 已记录 PASS；该检查在同一台 Windows 电脑完成，尚未在另一台全新系统上验证。

报告完成且源码已提交后，运行 `python -X utf8 scripts/bundle_delivery.py` 生成最终 ZIP。该脚本打包已提交源码、运行包、报告和证据，再解压 ZIP 验证程序；未提交源码时停止，避免提交编号与包内源码不一致。

## 报告和抓包

报告沿用仓库父目录的 `实验报告模板.docx`，依赖现有 `python-docx` 和 Pillow。使用已保存的日志、六张截图、运行包验证结果，以及存在时的抓包分析生成：

```powershell
python -X utf8 .\scripts\create_report.py
python -X utf8 .\scripts\create_report.py --template ..\实验报告模板.docx `
  --evidence ..\delivery\evidence --output ..\delivery\report\实验6_Socket通信编程实验报告.docx
```

已安装 Word 的 Windows 环境可执行 `powershell -File .\scripts\export_report.ps1` 导出 PDF，再逐页确认封面、中文、表格和截图没有溢出。生成脚本不编造缺失的抓包或测试结果，缺少的证据会标为待验证。姓名、学号及真实队内分工保留“待填写”，提交前按实际情况补齐。报告、截图、抓包和运行日志仅放在本地 `delivery/`，公开仓库只保留脱敏源码与文档。

已安装 Wireshark/Npcap 的 Windows 环境可重现实际抓包和分析：

```powershell
.\run_capture.ps1 -SkipBuild -WiresharkDir 'C:\Program Files\Wireshark'
```

按本机安装位置设置 WiresharkDir。脚本仅捕获 loopback 的实验端口：基础 18888、聊天室 18889，以及本次被叫临时监听端口；生成三份 pcapng、capture_analysis.json、tcp_analysis.md 和基础交流记录。这个复现步骤会重跑 GUI 演示并更新证据，之后应重新生成报告与 PDF。

手动演示抓包时，基础会话可过滤 `tcp.port == 8888`，聊天室可过滤 `tcp.port == 8889`，直接会话按被叫临时端口过滤。使用 `tcp.stream eq N` 和 Follow TCP Stream 检查握手、应用帧与关闭，不把某个抓包中的分段情况当成 TCP 的固定分帧方式。完整协议及分析要点见 [PROTOCOL.md](docs/PROTOCOL.md)，演示流程见 [DEMO.md](docs/DEMO.md)。

## 源码与交付目录

| 文件或目录 | 用途 |
| --- | --- |
| `tcp_basic_*.cpp`、`tcp_basic_common.h` | 基础 Winsock/POSIX TCP 文字收发及完整帧循环 |
| `main.cpp`、`mainwindow.*` | Qt 程序入口、图形服务器/客户端和界面状态 |
| `chatserver.*` | 登录、成员、群组、私聊、文件转发和呼叫信令 |
| `directcall.*` | 单个客户端直接 TCP 文字会话及关联超时 |
| `wire.h` | 聊天室 4 字节大端长度 + JSON 编解码，单帧 10 MiB |
| `tests/` | Python 网络集成测试及三个 QtTest 工程 |
| `run_build.ps1`、`run_tests.ps1`、`run_package.ps1` | 构建、验证和运行包生成入口 |
| `run_capture.ps1`、`scripts/analyze_captures.py` | 定向捕获实验端口并分析真实 TCP 流 |
| `scripts/create_report.py` | 读取实际证据并生成模板报告 |
| `scripts/export_report.ps1` | 使用已安装 Word 导出报告 PDF |
| `../delivery/runtime/` | EXE、依赖 DLL、插件和启动脚本 |
| `../delivery/evidence/`、`../delivery/report/` | 本地证据及 DOCX/PDF 报告 |

常见问题：端口占用时先停止对应实验实例或更换端口；昵称冲突时换昵称；未加入群时先加入；来电接听后失败时检查被叫临时端口是否可达。文件发送日志表示服务器已经转发，不代表接收方已经保存，接收窗口的保存结果才说明落盘成功。
