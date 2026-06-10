# Endpoint And Reply

本文总结 `kernel/object/endpoint.*` 与相关 syscall 路径。它描述了当前内核的 IPC 端点对象、同步/异步消息流，以及 `call/reply` 一次性回复能力的设计。

## 对象概览

这一部分实际上包含两类对象:

1. `EndpointPayload` / `EndpointObject`
2. `ReplyPayload` / `ReplyObject`

它们配合实现:

- 单向消息发送/接收
- 同步 rendezvous 风格发送
- 一次性 request/reply 调用
- 随消息传递 capability

## 消息结构

### `EndpointMsgView`

发送侧只使用轻量视图:

```cpp
struct EndpointMsgView {
    const char *msgbuf;
    size_t msgsz;
    CapIdx *capidxs;
    size_t capsz;
};
```

它不持有数据，只引用调用者提供的缓冲区和 capability 索引数组。

### `EndpointMessage`

真正进入内核队列的是 `EndpointMessage`:

- `sender_pid`
- 固定大小 `msgbuf`
- `msgsz`
- 固定大小 `capidxs`
- `capsz`

也就是说，消息内容会在发送时被复制进内核对象，而不是长期借用用户缓冲区。

## `EndpointPayload`

`EndpointPayload` 是端点本体，内部包含:

- 消息队列 `messages`
- 发送等待描述符 `send_wait_wd`
- 接收等待描述符 `recv_wait_wd`

### 语义

- `messages` 是真正的消息缓冲区
- 等待队列 ID 用于和 `task::wait` 子系统对接

析构时会把队列中尚未消费的所有 `EndpointMessage` 全部删除。

## `ReplyPayload`

`ReplyPayload` 是一次性回复对象。

它和普通 endpoint 最大的区别是:

- 只持有一条 `EndpointMessage *message`
- 只有一个接收等待描述符 `recv_wait_wd`

它专门服务于 `endpoint_call` 的调用方等待回复场景。

## 权限模型

### Endpoint 权限

定义在 `perm::endpoint`:

- `GRANT`
- `READ`
- `WRITE`

语义如下:

- `WRITE`: 允许发送消息
- `READ`: 允许接收消息
- `GRANT`: 允许随消息附带 capability

### Reply 权限

定义在 `perm::reply`:

- `CALLER`
- `REPLIER`

语义如下:

- `CALLER`: 允许读回复
- `REPLIER`: 允许写回复

## 异步发送

`EndpointObject::send_async(sender_pid, msg)` 的行为是:

1. 校验消息尺寸
2. 校验 `WRITE`
3. 若附带 capability，则校验 `GRANT`
4. 进入中断临界区
5. 只有在当前存在接收者等待时，才真正入队并唤醒一个接收者

### 返回值语义

返回 `Result<bool>`:

- `true`: 成功把消息放入队列并唤醒接收方
- `false`: 当前没有等待接收者，消息不会被缓存

因此它是严格意义上的“非阻塞尝试发送”，不是 mail-box 式的总是入队。

## 同步发送

`EndpointObject::send_sync(...)` 会保证消息最终被接收者取走。

### 流程

1. 校验消息与权限
2. 先构造完整 `EndpointMessage`
3. 进入临界区，把消息入队
4. 若有接收者等待，则唤醒一个
5. 若消息已被立即取走，则直接返回
6. 否则挂入 `send_wait_wd` 等待，直到消息不再在队列中

### 关键点

发送完成的判定不是“对方已经处理完业务”，而是:

- 该消息已经被对方从 endpoint 队列中取走

所以它是“同步交付到接收方”的语义，而不是 RPC 完成语义。

## 异步接收

`EndpointObject::recv_async()` 的行为是:

1. 校验 `READ`
2. 进入临界区
3. 队列为空时返回 `nullptr`
4. 否则弹出队首消息
5. 唤醒一个等待 `send_sync` 完成的发送者

因此接收一条消息会通知相应发送者“你的同步发送已经被接收端消费”。

## 同步接收

`EndpointObject::recv_sync()` 的行为是:

1. 校验 `READ`
2. 先尝试一次 `recv_async()`
3. 若为空，则挂入 `recv_wait_wd`
4. 被唤醒后再次尝试 `recv_async()`

所以它实现的是典型的“先快路径，再阻塞等待”的接收逻辑。

## `call/reply` 模型

`EndpointObject::call(...)` 是在 endpoint 上实现的一次性请求-回复协议。

### 目标

它要解决的问题是:

- 调用方向服务端发送请求
- 服务端以后要能把回复发回调用方
- 这个回复通道应当是一次性的
- 回复能力本身也应当通过 capability 机制传递

### 核心设计

实现方式是:

1. 创建一个 `ReplyPayload`
2. 在调用方 holder 中生成两个 capability:
3. `CALLER` 端留在本地等待回复
4. `REPLIER | MIGRATE_ONCE` 端附带进请求消息发送给服务端

这对临时槽位由 `create_reply_slots()` 创建。

### Reply 双端

生成的两个临时槽位:

- `caller`: `perm::reply::CALLER`
- `replier`: `perm::reply::REPLIER | perm::basic::MIGRATE_ONCE`

它们共享同一个 `ReplyPayload`。

### 调用流程

`EndpointObject::call()` 具体流程是:

1. 检查原始消息合法性
2. 创建 `ReplyPayload` 及 caller/replier 两端 capability
3. 把 replier cap 追加到请求消息 cap 列表末尾
4. 调用 `send_sync()` 把请求发给服务端
5. 请求成功被服务端取走后，释放本地 replier guard
6. 用 caller 端 `ReplyObject` 阻塞等待回复

返回值是服务端写回的 `EndpointMessage *`。

## `ReplyObject`

`ReplyObject` 是对 `ReplyPayload` 的能力操作封装。

### `send_reply(...)`

要求 `REPLIER` 权限。

行为:

1. 校验消息
2. 构造一条新的 `EndpointMessage`
3. 进入临界区
4. 如果 `ReplyPayload` 已经有消息，则返回 `false`
5. 否则把消息写入 `_obj->message`
6. 唤醒等待回复的 caller

这保证了一个 ReplyPayload 最多只持有一条未取走回复。

### `reply(...)`

`reply()` 是 `send_reply()` 的薄包装，额外会:

1. 要求写入回复成功
2. 从当前 holder 移除服务端持有的 `reply_cap`

因此 replier 端在成功回复后会被立即销毁，符合“一次性 reply capability”的语义。

### `recv_async()` / `recv_sync()`

caller 端使用:

- `recv_async()` 非阻塞读取 `_obj->message`
- `recv_sync()` 阻塞等待直到 `_obj->message != nullptr`

读取时会把 `ReplyPayload::message` 清空，确保消息只被取走一次。

## capability 随消息传递

Endpoint 本身只在消息里存 `CapIdx`，并不直接复制 capability 对象。

真正的跨 holder 传递发生在 syscall 接收路径。

### 发送侧

发送消息时，附带的是发送者 CHolder 中的 capability 槽位号。

### 接收侧

`kernel/syscall/endpoint.cpp` 中的 `insert_received_caps()` 会:

1. 通过 `sender_pid` 找到发送方的 `CHolder`
2. 对消息中每个 `CapIdx`
3. 调用 `sender_holder->transfer_to(*receiver_holder, capidx)`
4. 把接收到的新槽位号写回用户态消息包

所以随消息传递 capability 的真正语义由 `CHolder::transfer_to()` 决定:

- 有 `CLONE` 则复制
- 有 `MIGRATE` / `MIGRATE_ONCE` 则移动

### `MIGRATE_ONCE` 在 reply 里的作用

reply 流程专门使用了 `MIGRATE_ONCE`，因此:

- 调用方发请求时，replier capability 会被服务端收走
- 服务端收到的是去掉 `MIGRATE_ONCE` 后的新能力
- 本地原 slot 被消费
- 服务端只能用这份能力回复一次

这正是 call/reply 一次性通道的核心。

## syscall 层接口语义

与 endpoint 对应的 syscall 路径包括:

- `endpoint_create`
- `endpoint_send_async`
- `endpoint_recv_async`
- `endpoint_send_sync`
- `endpoint_recv_sync`
- `endpoint_call`
- `endpoint_reply`

其中接收路径在把内核消息写回用户缓冲区前，还会把附带 capability 转成接收方 holder 中的新槽位号。

## 同步语义总结

几种接口的同步边界不同:

- `send_async`: 只有接收方已在等待时才成功投递
- `send_sync`: 返回时表示消息已被接收方从 endpoint 队列取走
- `recv_sync`: 返回时得到一条消息
- `call`: 返回时得到完整回复
- `reply`: 返回时表示回复已写入 ReplyPayload，并消费服务端一次性 reply cap

## 当前设计特点

这套 endpoint 设计有几个明显特点:

- **权限细分**: 发送、接收、附带 capability、读回复、写回复分别授权
- **同步与异步并存**: 同一对象提供 try-send/try-recv 与阻塞式接口
- **回复通道 capability 化**: `call/reply` 不是特殊系统态路径，而是普通 capability 的一次性实例
- **capability 传递复用 holder 语义**: endpoint 不重新定义 capability 移动规则，而是直接调用 `transfer_to()`

它本质上把 IPC、reply 和 capability 传递统一到了同一套能力框架之上。
