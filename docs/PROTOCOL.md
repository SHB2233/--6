# TCP 应用协议

本文对应当前 Qt 5.15 实现。基础程序、聊天室和客户端直接文字会话都是 TCP；基础程序传文字负载，其他两种连接传 JSON 对象。TCP 提供有序字节流，应用层必须自行恢复消息边界。

## 帧格式

```text
0                   4                      4 + N
+-------------------+-------------------------+
| uint32 BE length N| N bytes of payload      |
+-------------------+-------------------------+
```

长度字段只计算负载字节数，不含四字节头；按网络字节序，即大端编码。Qt 负载为 compact JSON 的 UTF-8 字节，必须是 JSON 对象。接收缓冲区不足四字节或不足 N 字节时等待下一次 readyRead；缓冲完整时取出一帧并继续循环处理后续帧。一次 write、一次 readyRead 与一个 TCP 包没有一一对应关系。

| 连接 | 默认端口 | 允许负载长度 | 内容 |
| --- | --- | --- | --- |
| 基础文字 | TCP 8888 | 1～1 MiB | 文字字节；`/quit` 结束 |
| 客户端到聊天室 | TCP 8889 | 1～10 MiB | compact UTF-8 JSON 对象 |
| 客户端到客户端 | 被叫临时 IPv4 端口 | 1～10 MiB | direct call JSON 对象 |

例如 `{"type":"join","nickname":"Alice"}` 的 UTF-8 长度为 34，其帧头为 `00 00 00 22`。中文按 UTF-8 编码后的字节数计入帧长度，不能使用字符串显示字符数代替。

## 昵称登录与在线成员

客户端 TCP 建连成功后发送 join，收到 join_ok 后才进入可发消息的登录状态。这里的登录是昵称登记，没有账户密码认证。

```json
{"type":"join","nickname":"Alice"}
{"type":"join_ok","nickname":"Alice"}
{"type":"users","users":["Alice","Bob"]}
```

昵称先 trimmed，长度按 Qt QString::size() 计算，为 1～24 个 UTF-16 代码单元；内部不能有 QChar::isSpace() 判定的空白。在线昵称不区分大小写去重。非法昵称返回 error 并断开。未登录时发送业务帧返回错误；重复 join 不重复登记。用户退出后从在线列表和全部群组成员中移除。

通用服务器提示为 `{"type":"system","text":"..."}`，错误为 `{"type":"error","text":"..."}`。聊天室连接或登录限时 10 秒；连接失败后客户端恢复可重试状态。

## 群组 群聊与私聊

新用户自动加入 lobby（公共大厅）。创建群组时使用服务器生成的 UUID 作为 groupId；群名称 trimmed 后为 1～32 个 UTF-16 代码单元，不能与已有群名重复（不区分大小写）。组状态仅存内存。

| 方向 | type | 关键字段与行为 |
| --- | --- | --- |
| 客户端→服务器 | group_create | name；创建成功自动加入新群 |
| 客户端→服务器 | group_join | groupId；加入已存在群 |
| 服务器→客户端 | group_joined | groupId、name，确认加入 |
| 服务器→客户端 | groups | groups 数组，各项有 id/name/members/joined |
| 客户端→服务器 | chat | groupId、text；缺少 groupId 时默认 lobby |
| 服务器→群成员 | chat | from/groupId/groupName/text，包含发送者 |
| 客户端→服务器 | private | to、text；收件人须在线 |
| 服务器→双方 | private | from/to/text；发送者与目标各一份 |

非群成员不能向该群发送文字或文件。私聊通过原服务器连接转发，不建立直接 TCP 通道，与 /w 昵称 消息界面语法对应。私聊目标按昵称不区分大小写匹配；消息仅发给发送者和目标。

## 文件

```json
{"type":"file","groupId":"lobby","name":"sample.bin","data":"AEIK"}
```

data 为文件原始字节的 Base64。服务器提取文件 basename，严格解码 Base64，并按解码后的原始长度检查 ≤ 5 MiB；空文件合法。转发帧补充 from/groupId/groupName，只转发给同群的其他成员。Base64 约增大三分之一，仍受 10 MiB 的 JSON 帧上限约束。

发送者收到“已转发给 N 位其他成员”系统消息，它表示转发结果，未包含对方保存确认。客户端默认询问保存位置，允许取消；--download-dir 是主动开启的自动保存目录，遇到重名加数字前缀。保存使用 QSaveFile，写入或提交失败时显示失败。

## 呼叫信令与直接文字会话

服务器保留每人最多一个待接听或活动呼叫。主叫、被叫必须已登录且在线；不能呼叫自己。callId 非空、最多 64 个 UTF-16 代码单元且不能重复，GUI 用 UUID。

```text
主叫                    聊天服务器                    被叫
  | call_request(id,to)     |                           |
  |<--- call_ringing ------ | --- call_incoming(id,from)>|
  |                        |             接听并监听临时IPv4端口
  |                        |<--- call_response(id,accept,port)
  |<-- call_accepted(id,host,port,from)                   |
  |                                                     |
  |============= 新TCP连接到被叫临时端口 ===============>|
  |------------------- call_hello(id) ------------------>|
  |<------------------ call_ready(id) -------------------|
  |<================= call_chat(text) ==================>|
  |--------------------- call_end --------------------->|
```

call_accepted.host 来自服务器看到的被叫源 IPv4；这适用于当前同机/可达局域网设计，不提供 NAT 穿透。被叫只有实际监听成功才用 accept:true 回应。主叫直连首帧为 call_hello，被叫确认 callId 后回 call_ready，两帧均带 callId；只有关联成功才认为文字会话已连接。

| 场景 | 行为 |
| --- | --- |
| 30 秒未接听 | 服务器向双方发 call_ended，reason 为未接听 |
| 被叫拒绝/监听端口非法 | 结束呼叫，释放忙线状态 |
| 主叫取消/任一方挂断 | 向服务器发 call_cancel 或 call_end，服务器转发 call_ended |
| 用户离线/服务器停止 | 清除关联呼叫并通知另一方 |
| 接听后直连或关联超过 10 秒 | DirectCall 关闭监听/socket，发 error 和 ended |
| 被叫遇到错误首帧或 callId | 拒绝候选连接，仍允许在原超时内合法重试 |
| 直连关联后 | call_chat 携带 text；call_end 正常结束 |

信令通道上的 call_end 需要 callId；直接连接上的 call_end 不需要 callId，因为 socket 已经与一个呼叫关联。挂断直接会话后，聊天室仍可继续群聊；服务器连接断开则清理对应直接会话。

## 抓包判断

在 loopback 抓包中，源/目的地址均可能为 127.0.0.1，用端口和 tcp.stream 区分连接。常用 Wireshark 显示过滤器：

```text
tcp.port == 8888
tcp.port == 8889
tcp.stream eq N
tcp.flags.syn == 1
tcp.flags.fin == 1 || tcp.flags.reset == 1
```

自动测试可使用其他端口，以实际捕获端点为准。三次握手检查 SYN、SYN/ACK、ACK；数据阶段按序列号重组，再读取四字节帧头和 UTF-8 JSON；关闭阶段按实际抓到的 FIN/ACK 或 RST 说明，不能默认每条连接一定独立出现四个关闭包。ACK 仅确认 TCP 字节，不代表用户已经阅读消息或保存文件。

若抓包中的直接 call_chat 在非服务器端口的临时流内，而原服务器流中只有呼叫信令，就能支持“直接文字走客户端之间的新连接”的结论。抓包明文可看到昵称与文字，当前实现没有 TLS 加密。
