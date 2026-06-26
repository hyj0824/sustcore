# PCB And TCB Objects

本文总结 `kernel/object/task.*`。这部分把进程/线程控制块暴露成 capability 对象，供 syscall 与其它内核路径通过统一权限接口操作进程。

## 对象类型

这一层定义了两种 payload:

- `PCBPayload`
- `TCBPayload`

以及两种对象封装:

- `PCBObject`
- `TCBObject`

当前真正有较完整操作语义的是 `PCBObject`。

## `PCBPayload`

`PCBPayload` 持有:

- `task::PCB *pcb`

### 特殊销毁语义

`PCBPayload::destruct()` 不直接删除 PCB，而是:

1. 调用 `TaskManager::inst().enqueue_recycle(pcb)`
2. 再 `delete this`

因此:

- PCB 的真正回收是延迟的
- 最后一个 PCB capability 消失时，只是把 PCB 送进任务管理器回收队列

这说明 PCB 生命周期不仅受 capability 数量影响，也受调度/任务系统的回收时机控制。

## `TCBPayload`

`TCBPayload` 只保存:

- `task::TCB *tcb`

当前没有额外覆写析构，也没有复杂对象方法，更多是为未来线程级 capability 操作预留接口。

## 权限模型

进程相关权限定义在 `perm::pcb`:

- `GETPID`
- `KILL`
- `VMCONTEXT`
- `NEW_THREAD`
- `NEW_PROCESS`
- `EXECUTE`

这些权限都在 `PCBObject` 内部检查。

## `PCBObject`

`PCBObject` 是 `CapObj<PCBPayload>` 的具体对象封装。

它提供:

- `get_pid()`
- `kill(exit_code)`
- `map(memory, offset, vaddr, sz, protflg)`
- `unmap(vaddr, sz)`
- `query_vaddr(vaddr, mem_cap)`
- `query_vspace(offset, max_entries, expose_mem_cap)`
- `require_new_thread()`
- `require_new_process()`
- `require_execute()`
- `require_new_process_execute()`

## `get_pid()`

要求 `GETPID` 权限。

若:

- 权限不足 -> `INSUFFICIENT_PERMISSIONS`
- `pcb == nullptr` -> `NULLPTR`

成功时返回目标进程 PID。

## `kill(exit_code)`

要求 `KILL` 权限。

### 核心行为

1. 设置 `pcb->exit_code`
2. 若已在 `exiting` 状态，直接返回
3. 标记 `pcb->exiting = true`
4. 清空该进程的 `CHolder`
5. 遍历所有线程
6. 将其他 READY 线程从调度器中移除
7. 把线程状态改成 `DYING`
8. 若正在杀当前进程，则标记需要重调度并立即 `schedule()`

### 对能力系统的影响

`pcb->cholder->clear()` 会清空整个能力空间，这意味着:

- 进程被 kill 后，它持有的 endpoint、memory、vfile 等能力都会被释放
- 若这些能力是某些 payload 的最后一个引用，就会触发对应 payload 的销毁逻辑

所以 `kill()` 同时也是 capability 释放入口。

## `map(memory, offset, vaddr, sz, protflg)`

要求 `VMCONTEXT` 权限。

它并不直接操作页表，而是:

1. 检查 PCB 是否存在及其 `tmm` 是否存在
2. 校验 offset、地址和大小的页对齐与范围合法性
3. 通过目标 TMM 的 `add_vma(...)` 建立映射

因此:

- PCB capability 负责授权“可以操作这个进程的地址空间”
- Memory capability 继续负责授权“这段 Memory 允许怎样映射”

这是两个 capability 的权限叠加。

## `unmap()` 与查询接口

`unmap(vaddr, sz)` 同样要求 `VMCONTEXT`，会调用目标 TMM 的
`remove_vma_range()`。

`query_vaddr()` 与 `query_vspace()` 也要求 `VMCONTEXT`，并把 TMM 查询结果包装为 `VMAInfo` 供 syscall 层返回。

## `require_*` 系列接口

这几组接口本身不做复杂业务，而是作为 syscall 或任务管理路径的权限闸门。

### `require_new_thread()`

要求 `NEW_THREAD` 权限，成功返回 `task::PCB *`。

### `require_new_process()`

要求 `NEW_PROCESS` 权限，成功返回 `task::PCB *`。

### `require_execute()`

要求 `EXECUTE` 权限，成功返回 `task::PCB *`。

### `require_new_process_execute()`

要求同时具备:

- `NEW_PROCESS`
- `EXECUTE`

成功返回 `task::PCB *`。

### 它们的意义

这些函数把权限校验与对象查找统一到了 capability 层，避免 syscall 层自己散落地检查位图。

## `current_object_tcb()`

`task.cpp` 里有一个内部辅助函数 `current_object_tcb()`。

它当前直接退化为 `Scheduler::inst().current_tcb()`。

这主要服务于 `kill()`，使它在普通路径和 syscall 协程路径下都能正确判断“是否正在杀当前进程”。

## `TCBObject`

`TCBObject` 目前只是一个最薄封装:

```cpp
class TCBObject : public CapObj<TCBPayload> { ... }
```

还没有扩展出线程级操作接口。

这说明当前 capability 模型主要围绕进程对象展开，线程对象能力尚处于预留状态。

## 与能力系统的关系

Task 对象和其它 capability 对象相比，有两个特殊点:

1. `PCBPayload` 的销毁不是立即 delete PCB，而是交给 `TaskManager`
2. `kill()` 会反向影响能力系统，把整个进程的 CHolder 清空

因此 task capability 是能力系统和任务系统的交叉点。

## 当前设计特点

这部分设计体现了几个原则:

- **进程控制权 capability 化**: 是否能 kill、map、spawn、exec 完全由权限位决定
- **生命周期延迟回收**: PCB 销毁交由任务管理器统一处理
- **权限检查集中在对象层**: syscall 不直接理解这些细节
- **地址空间操作需要双重授权**: 同时要有 PCB 的 VMCONTEXT 与 Memory 的映射权限

在当前仓库里，`PCBObject` 是任务管理相关能力操作的主要入口。
