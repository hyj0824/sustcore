# Notification Object

本文总结 `kernel/object/notif.*`。Notification 是一类轻量同步对象，用位图表示多个独立信号位，并对每个信号位分别做权限控制。

## 总体模型

`NotificationPayload` 表示一组二值信号，每个信号位都可以被:

- 置位
- 清位
- 查询
- 等待

和 endpoint 相比，它不承载消息内容，也不承载 capability 传递；它更像一个“带权限分片的多路事件标志组”。

## `NotificationPayload`

它包含两个核心字段:

- `b32 signalbits`
- `wait::wd_t wait_wds[perm::notif::MAX_SIGNALS]`

### `signalbits`

当前实现中信号位图用 `b32` 保存，注释说明“实际长 24 位”。  
每一位表示一个 signal 当前是否被置位。

### `wait_wds`

每个 signal 都有自己独立的等待描述符，因此不同信号的等待者不会互相干扰。

构造时会为每个信号调用 `wait::alloc_reason()`。

## 权限模型

Notification 的权限设计与其它对象不同，不是整对象一组 READ/WRITE 位，而是:

- 每个信号占 2 位权限
- 分别表示 `SIGNAL` 与 `QUERY`

定义在 `perm::notif`:

- `SIGNAL`
- `QUERY`
- `BITS = 2`
- `STARTBITS = 16`

### 计算方式

通过:

- `bitoffset(idx)`
- `perm(permbits, idx)`

可以从 capability 总权限值里提取某个 signal 的 2 位权限。

这意味着两个 capability 即使指向同一个 NotificationPayload，也可以对不同 signal 拥有不同的能力。

## `NotificationObject`

对象操作层提供:

- `signal(idx)`
- `unsignal(idx)`
- `set(idx, state)`
- `query(idx)`
- `wait(idx)`

其中:

- 前四个接口仍然同步返回 `Result<bool>`
- `wait(idx)` 当前返回 `wait::cotask<Result<bool>>`

## 下标合法性

所有操作都会先调用 `check_idx(idx)`，要求:

```cpp
idx < perm::notif::MAX_SIGNALS
```

否则返回 `OUT_OF_BOUNDARY`。

## `signal(idx)`

要求当前 capability 对该 signal 拥有 `SIGNAL` 权限。

行为:

1. 进入中断临界区
2. 把对应位设置为 1
3. `wake_all(wait_wds[idx])`

返回值为 `true`。

### 语义

被 signal 后:

- 当前 signal 处于置位状态
- 所有等待该 signal 的线程都会被唤醒

它不会自动清除。

## `unsignal(idx)`

同样要求 `SIGNAL` 权限。

行为:

1. 进入临界区
2. 把对应位清零

返回值为 `false`。

## `set(idx, state)`

`set()` 是 `signal/unsignal` 的统一版本:

- `true` 时置位并唤醒等待者
- `false` 时清位

仍然要求 `SIGNAL` 权限。

## `query(idx)`

要求 `QUERY` 权限。

行为:

1. 进入临界区
2. 读取信号位当前状态

返回:

- `true`: 已置位
- `false`: 未置位

## `wait(idx)`

`wait()` 也要求 `QUERY` 权限，而不是 `SIGNAL` 权限。

### 行为

当前实现已经改为 coroutine 等待路径，大致流程是:

1. 检查 `idx` 与 `QUERY` 权限
2. 在循环内进入临界区
3. 如果对应位已经为 1，则立即 `co_return true`
4. 否则 `co_await wait::FutureAwaiter(...)`
5. 被恢复后重新检查 signalbit，直到该位为 1

### 重要语义

当前实现的 wait:

- 不会自动清除信号位
- 只是等待“某个时刻该信号被置位”

因此它更接近 level-triggered 事件，而不是 auto-reset event。

## 同步原子性

`wait()` 的实现特别强调了一个点:

- 对象层仍然需要在临界区内检查 signalbit，避免和 signal/unsignal 并发修改
- 真正的线程等待登记不再由 `NotificationObject::wait()` 自己完成
- `FutureAwaiter` 只负责 suspend coroutine 并写入 `WaitContext`
- 最外层 syscall 路径再根据 `wait_wd` 统一调用
  `wait::register_syscall_wait(...)`

因此当前 notif wait 的 race-safe 依赖于:

1. 对象层的临界区状态检查
2. `FutureAwaiter` 的 ready predicate
3. 外层统一的等待登记路径

## 权限分片的意义

Notification 的设计重点不是复杂状态，而是“按 signal 切分权限”。

这允许构造很多细粒度同步模式，例如:

- 一个 capability 只能等待某几个 signal
- 另一个 capability 只能触发另一组 signal
- 同一个 NotificationPayload 作为多方共享事件对象使用

## 与其它对象的区别

和 endpoint 相比:

- notif 不传输消息
- notif 不传输 capability
- notif 的同步单位是“位”

和 mutex 相比:

- notif 可以同时表示多个独立事件
- notif 不保证互斥，也不记录拥有者

## 当前设计特点

Notification 对象的特点是:

- **每个 signal 单独授权**
- **等待与置位是 race-safe 的**
- **signal 后默认保持置位**
- **语义简单，适合作为轻量事件对象**

它本质上是“带 capability 权限切片的位图事件组”。
