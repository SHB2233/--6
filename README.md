# 实验 6：Socket 通信编程

本项目按实验要求包含两个部分：

- `tcp_basic_server.cpp` / `tcp_basic_client.cpp`：基础 TCP 一对一文字通信示例，对应实验第一部分。
- `mainwindow.cpp` / `mainwindow.h`：Qt 图形聊天室，支持多人广播、昵称登录、私聊和文件发送，对应实验第二部分，并可作为可视化演示。

TCP is a byte stream, so each program uses a 4-byte network-order length prefix to mark message boundaries. The Qt chat carries UTF-8 JSON frames; the basic demo carries text frames.

## 一、运行基础 TCP 示例

需要 C++17 编译器。Windows 上可用 MinGW-w64：

```powershell
g++ -std=c++17 tcp_basic_server.cpp -o tcp_basic_server.exe -lws2_32
g++ -std=c++17 tcp_basic_client.cpp -o tcp_basic_client.exe -lws2_32
```

也可用 Visual Studio 开发者命令提示符：

```bat
cl /std:c++17 /EHsc tcp_basic_server.cpp ws2_32.lib
cl /std:c++17 /EHsc tcp_basic_client.cpp ws2_32.lib
```

先在第一个终端启动服务器：

```powershell
.\tcp_basic_server.exe 8888
```

服务器收到客户端消息后会等待你输入回复。再打开第二个终端启动客户端：

```powershell
.\tcp_basic_client.exe 127.0.0.1 8888
```

输入文字并回车即可收发；输入 `/quit` 结束会话。若客户端和服务器不在同一台电脑，把 `127.0.0.1` 替换为服务器的局域网 IPv4 地址，并允许防火墙接收该 TCP 端口。

## 二、构建并运行 Qt 图形聊天室

需要 Qt 5.15 或 Qt 6 的 **Widgets 和 Network** 模块，以及与 Qt 安装包匹配的编译器工具链。推荐用 Qt Creator 打开 `qt_chat.pro`，选择匹配的 Desktop Kit 后构建并运行。也可以在对应的 Qt 命令行环境下构建：

```powershell
qmake qt_chat.pro
mingw32-make
```

如果使用 MSVC 版 Qt，应在 Visual Studio 开发者命令提示符中运行 `qmake qt_chat.pro` 和 `nmake`，不要混用 MSVC Qt 与 MinGW 编译器。

使用步骤：

1. 启动一个程序实例，将模式选为“服务器”，端口保持 `8888`，点击“启动服务器”。
2. 再启动一个或多个实例，模式选“客户端”，填写昵称。单机测试使用 `127.0.0.1`；局域网测试填写服务器电脑的 IPv4 地址，然后点击“连接服务器”。
3. 在输入框中发送群聊消息。私聊格式为 `/w 昵称 消息`，例如 `/w Alice 你好`。
4. 点击“发送文件”，选择不超过 5 MB 的文件；其他在线客户端会收到保存文件对话框。
5. 服务器窗口的右侧用户列表由客户端状态更新；点击“断开/停止”可断开客户端或关闭服务器。

局域网连接时，需要在服务器电脑的防火墙中允许所用 TCP 端口（默认 8888）。首次演示建议先在同一台电脑开一个服务器和两个客户端窗口，再测试广播、私聊和文件发送。

## 文件说明

- `qt_chat.pro`：Qt 图形程序工程文件。
- `tcp_basic_common.h`：基础 TCP 示例共用的跨平台 socket 和完整帧收发函数。
- `basic_tcp_server.pro`、`basic_tcp_client.pro`：可选的 qmake 控制台工程配置。
