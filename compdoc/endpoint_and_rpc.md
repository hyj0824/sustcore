# Endpoint Capability

Endpoint(端点) 能力是内核的核心概念之一, 它是内核中用于实现进程间通信(IPC)的能力. 每个进程都可以创建一个 Endpoint 对象, 并通过该对象与其他进程进行通信. 虽然内核当前尚未实现用户态驱动(对于用户态驱动, Endpoint 能力是必须的), 但 Endpoint 能力与 librpc 库已经实现了基本的通信与 RPC 功能(Remote Procedure Call, 远程过程调用).

## Endpoint 的基本系统调用

Sustcore 当前围绕 Endpoint 提供了两组最核心的接口:

1. `endpoint_create`
   用于创建一个新的 Endpoint capability.
2. `endpoint_send` / `endpoint_recv`
   用于同步地发送和接收消息.
3. `endpoint_send_async` / `endpoint_recv_async`
   用于非阻塞地尝试发送和接收消息.
4. `endpoint_call` / `endpoint_reply`
   用于一次性的请求-回复式调用.

Endpoint 主要作为一个信息通道存在.
通信双方需要显式构造 `MsgPacket`, 并通过系统调用把消息交给内核进行传递.
消息本身既可以只包含普通字节数据, 也可以额外附带 capability (Capability Grant, 参考自 seL4).

因此, Endpoint 是 sustcore 中未知进程之间交换 Capability 的最主要手段, 也是构建用户态服务框架的基础设施之一.

### 同步与异步

Endpoint 的同步/异步区别表达了两种不同的通信意图.

1. 同步发送/接收强调对端已经参与了这次通信, 适合更强的配合式 IPC.
2. 异步发送/接收强调只做一次尝试, 当前条件不满足就立即返回, 适合轮询或更灵活的调度配合.

因此, Sustcore 避免将 Endpoint 设计成一个无限缓存的邮箱.
其更偏向一种受控的消息交付机制, 使得内核中的资源消耗是可控的.

### capability 传递

Endpoint 的另一个关键能力是随消息传递 capability. 这意味着一个进程不仅可以告诉另一个进程 "请替我做某事", 还可以同时把完成该事情所需的某个对象能力一并交给对方.

例如:

1. 把某个 VFile capability 交给另一个进程, 使其能够继续操作该文件.
2. 把某个 Memory capability 交给另一个进程, 使其能够映射或共享该内存对象.
3. 把某个 Reply capability 临时交给服务端, 使其在稍后对本次请求进行回复.

这一设计与 Sustcore 的整体能力模型是统一的: 资源不会凭空"可见", 而必须通过显式的 capability 传递链路到达目标进程.

### 当前定位

从现有源码来看, Endpoint 已经足以支撑基本 IPC 测试、简单的服务调用模型以及 librpc 这样的上层协议封装.
目前 `module/test_endpoint_master` / `module/test_endpoint_slave` 展示的是最基础的消息传递用法, 而 `module/test_call_user` 和相关服务测试则展示了 `endpoint_call` / `endpoint_reply` 所形成的一次性调用模型.

因此, Endpoint 在 Sustcore 当前阶段的定位可以概括为:

1. 作为最基础的用户态 IPC 原语.
2. 作为 capability 跨进程传递的主要通道之一.
3. 作为上层 RPC 与潜在用户态服务框架的基础设施.

## Call / Reply 模型

单纯的 `send` / `recv` 只能表达 "把一条消息交给对方", 但大多数的服务交互(即 RPC 交互)的本质是远程发送一个过程调用请求并获取返回值. 这正是 `endpoint_call` / `endpoint_reply` 解决的问题.

Sustcore 通过引入一次性的 Reply capability 来解决这一问题. 调用方在执行 `endpoint_call` 时, 内核会为这次调用创建一个临时的回复通道, 然后把服务端用于回复的那一侧 capability 附带在请求消息中发送出去. 服务端收到请求后, 就可以在适当的时候通过 `endpoint_reply` 把结果返回给调用方.

这种做法有几个明显优点:

1. Reply 通道是一次性的, 生命周期清晰, 不容易和其他请求混淆.
2. 回复能力本身也是 capability, 因此它仍然遵守 Sustcore 一贯的权限与转移模型.
3. `call` / `reply` 复用了 Endpoint 的消息传递与 capability 传递机制, 因而能够自然纳入现有 IPC 模型.

因此, Sustcore 当前的"调用"语义可以理解为"带一次性回复通道的 IPC".

## LibRPC

librpc 是构建在 Endpoint 之上的 RPC 封装库. 从内核视角看, 它主要完成以下工作:

1. 规定一套在 `MsgPacket.msgbuf` 中编码请求/响应的方法.
2. 约定服务名、服务 magic、函数编号、参数类型与返回值的组织方式.
3. 封装客户端发起调用、服务端接收与分发请求、以及双方的错误处理流程.

librpc 与 Endpoint 可以从两个层次来理解:

1. Endpoint 提供可靠的受控消息传递与 capability 传递能力.
2. librpc 则在此基础上约定怎样把一次函数调用描述成消息, 又怎样把返回值描述成响应.

仅使用 Endpoint 时, 用户仍然需要自己约定消息格式、函数编号与参数布局. librpc 负责把这些重复性的协议工作收敛为统一约定与辅助接口.

### librpc 的基本思路

当前 librpc 的核心目标是提供一个足够轻量、足够贴合 Sustcore 现状的 RPC 封装.

其基本思路可以概括为:

1. 用 `service_magic` 区分不同服务, 避免不同服务误收消息.
2. 用 `function_id` 标识要调用的接口.
3. 用一套固定的参数编码规则把基本类型和部分 POD 数据打包进消息缓冲区.
4. 通过 `endpoint_call` / `endpoint_reply` 获得天然的一问一答式调用体验.

因此, 当前 librpc 是一个 "结构化消息 + 自动分发" 的工具封装.

### 无反射时的基本使用

在没有更强的静态描述能力时, librpc 仍然可以作为一层显式的协议库使用. 此时, 用户需要自己维护服务端的分发逻辑、消息类型检查以及参数编码/解码流程.

一个典型的服务端主循环大致如下:

```cpp
create endpoint

while (true) {
    recv(endpoint, &request)

    if (!is_rpc_message(request)) {
        continue
    }

    packet = decode_request(request)
    switch (packet.function_id) {
    case FUNC_A:
        args = parse_args(packet)
        result = service.func_a(args...)
        reply  = encode_response(result)
        endpoint_reply(reply_cap, &reply)
        break

    case FUNC_B:
        args = parse_args(packet)
        result = service.func_b(args...)
        reply  = encode_response(result)
        endpoint_reply(reply_cap, &reply)
        break
    }
}
```

客户端侧的调用流程则大致如下:

```cpp
request = encode_request(service_magic, function_id, args...)
reply   = endpoint_call(endpoint, &request)
result  = decode_response(reply)
return result
```

这一模式的特点是简单直接, 也最贴近 Endpoint 的原始工作方式. 其代价是用户需要自己维护较多样板代码, 包括:

1. 函数编号与服务 magic 的一致性.
2. 参数的编码与解码逻辑.
3. 参数类型与消息布局的一致性.
4. 服务端分发与错误处理逻辑.

### 基于接口描述的模板封装

librpc 已经提供了 `MetaServer`、`MetaClient` 等基于反射机制的模板封装, 使用户可以用接近"定义接口类 + 实现类"的方式来编写 RPC 服务.
我们希望通过这类封装减少服务端和客户端对同一份协议的重复手写, 降低"函数编号写错""参数布局不一致"这类机械性错误.

### librpc 的进一步改造

当前 librpc 已经引入了 session 相关概念, 但整体上仍然偏向在 Endpoint 上做轻量 RPC.
后续继续演进的一个期望方向是, 把现在的处理模式扩展为 per-Session 的对话模型.

这样做的目的主要是让同一服务能够同时维护多个彼此隔离的会话状态,
以让服务端更自然地表达"建立会话 - 多次调用 - 关闭会话"的交互过程,
为更复杂的用户态服务、长期连接以及状态化协议提供基础.