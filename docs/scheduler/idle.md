# IDLE Scheduler

本文总结 `kernel/schd/idle.h` 中的 IDLE 调度类。IDLE 是最低优先级调度类，用于系统没有其它可运行线程时运行 idle 内核线程。

## 基本属性

调度类:

```cpp
constexpr static ClassType CLASS_TYPE = ClassType::IDLE;
```

IDLE 调度类不使用 `RQ` 链表，而是维护一个单独指针:

```cpp
SchedMeta *ready = nullptr;
```

## 队列行为

### enqueue

入队时:

1. 将实体状态设为 `READY`。
2. 将 `ready` 指向该实体。

### dequeue

出队时:

1. 要求 `ready` 正好等于目标实体。
2. 清空 `ready`。
3. 将状态设为 `EMPTY`。

### pick_next

选择线程时:

1. `ready == nullptr` 返回 `ErrCode::NO_RUNNABLE_THREAD`。
2. 取出 ready 实体。
3. 清空 ready。
4. 状态设为 `RUNNING`。
5. 更新 `cursched`。

### put_prev

`put_prev()` 把 idle 线程重新设为 ready，并记录回唯一的 ready 指针。

## 抢占

`check_preempt_curr()` 固定返回 true。只要有更高优先级线程被唤醒，当前 IDLE 线程就应被抢占。

## idle 线程入口

IDLE 线程由 `TaskManager::create_idle_thread()` 创建，入口为:

```cpp
while (true) {
    Riscv64Idle::idle();
}
```

调度器初始化时会把 idle TCB 设为当前运行线程，并设置 `NEED_RESCHED`，让系统启动后尽快切到 init 线程。

## 当前限制

IDLE 当前是单槽模型，只支持一个 idle 线程。多 hart 场景下需要为每个 hart 准备独立 idle TCB 和运行队列。
