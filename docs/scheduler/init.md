# INIT Scheduler

本文总结 `kernel/schd/init.h` 中的 INIT 调度类。INIT 是系统启动阶段的特殊调度类，用于承载 init 进程主线程。

## 基本属性

调度类:

```cpp
constexpr static ClassType CLASS_TYPE = ClassType::INIT;
```

优先级高于 `RR`，低于 `RT`。

INIT 当前内部维护两个 ready 槽:

```cpp
SchedMeta *kinit_ready = nullptr;
SchedMeta *init_ready = nullptr;
```

## 队列行为

### enqueue

将线程设为 `READY`，并按 `BootThreadRole` 记录到 `kinit_ready` 或 `init_ready`。

### dequeue

要求目标实体位于对应 ready 槽中，然后清空该槽并设为 `EMPTY`。

### pick_next

若 ready 槽存在，则优先取 `kinit_ready`，否则取 `init_ready`，并更新 `cursched`。

### put_prev

把当前 init 线程重新设为 `READY` 并记录回对应 ready 槽。

## tick 与 yield

`on_tick()` 不做操作。INIT 不使用时间片。

`yield()` 会设置当前实体 `NEED_RESCHED`。

## 创建路径

`TaskManager::create_init_task()` 固定使用:

```cpp
constexpr schd::ClassType INIT_SCHED_CLASS = schd::ClassType::INIT;
```

普通用户进程不能通过 `create_task()` 请求 INIT 调度类。fork 时如果父线程属于 INIT，子线程会降级为 FCFS，避免普通子进程继续占用 INIT 类。

## 当前限制

INIT 当前不是单一 ready 槽，而是按 `BootThreadRole` 区分 `KINIT` 和
`INIT_USER` 两类启动线程；它仍然更像启动阶段的特殊调度类，而不是通用高优先级调度策略。
