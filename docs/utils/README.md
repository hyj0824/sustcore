# Utility Components

本文档汇总 Sustcore 中不直接归属于能力、设备、任务、VFS 等业务子系统的基础工具。它们主要分布在 `include/sus/`、`include/sustcore/`、`include/std/c++/` 以及少量内核辅助头文件中。

## 文档索引

- [错误处理流程与辅助接口](error_processing/error_processing.md)
- [容器总览](containers/containers.md)
- [链表与数组列表](containers/list.md)
- [映射、队列与标准库兼容容器](containers/map_queue.md)
- [树结构工具](containers/tree.md)
- [静态反射与运行时类型检查](reflection.md)
- [所有权、非空指针与 RAII](ownership.md)
- [协程工具](coroutine.md)
- [原子操作与自旋锁](atomic_spinlock.md)
- [杂项工具](misc.md)

## 使用原则

这些工具的共同目标是让内核代码在不使用 C++ 异常的前提下表达清楚错误、所有权、对象类型与集合关系。

实际开发时应优先遵守以下原则:

- 返回可恢复错误时使用 `Result<T>` 或对应的无异常结果类型。
- 裸指针默认只表示地址；需要表达所有权时使用 `util::owner<T *>`，需要表达非空前置条件时使用 `util::nonnull<T *>`。
- 需要回滚的多步骤构造流程使用 `util::Guard`、`delete_guard`、`remove_guard` 等作用域守卫。
- 单个共享标量优先使用 `std::atomic<T>`；共享容器和复合状态优先使用自旋锁；只关闭本地中断时使用 `InterruptGuard`。
- 调度队列、对象树、等待队列等内核对象集合优先使用侵入式结构，避免隐藏分配和额外所有权。
- `std::array`、`std::unordered_map`、`std::ranges` 等标准库兼容层是内核内可用的简化实现，不应假设其完整覆盖宿主标准库行为。

## 主要源码位置

- `include/sustcore/errcode.h`: 内核统一错误码、`Result<T>` 与错误传播宏。
- `include/std/c++/expected`: 项目内 `std::expected` 实现及链式处理扩展。
- `include/sus/list.h`: 侵入式链表、普通链表、动态数组列表。
- `include/sus/map.h`: 小规模链式映射。
- `include/sus/queue.h`: 固定容量静态环形队列。
- `include/sus/tree.h`: 侵入式树和继承式树结构工具。
- `include/sus/rtti.h`: 无 RTTI 环境下的类型标识与安全转换辅助。
- `include/sus/owner.h`、`include/sus/nonnull.h`、`include/sus/raii.h`: 所有权标注、非空指针和 RAII 工具。
- `include/sus/coroutine.h`: `util::cotask<T>` 协程任务类型。
- `include/std/c++/atomic`、`kernel/spinlock.h`: 原子操作与自旋锁基础实现。
- `include/sus/path.h`、`include/sus/range.h`、`include/sus/units.h`、`include/sus/logger.h`: 路径、区间、单位和日志等杂项工具。
