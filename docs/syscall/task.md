# Task Syscalls

本文总结 `kernel/syscall/task.*` 中的进程、线程和地址空间相关 syscall。syscall 层只负责参数预处理、用户缓冲区同步和 capability lookup；权限检查集中在 `PCBObject`、`MemoryObject` 等 capability 对象中。

## 当前接口

任务相关 syscall helper 包括:

- `pcb_create_process`
- `pcb_create_linux_process`
- `pcb_create_thread`
- `tcb_wait`
- `tcb_nanosleep`
- `pcb_fork`
- `pcb_kill`
- `pcb_map`
- `pcb_unmap`
- `pcb_query_vaddr`
- `pcb_query_vspace`
- `pcb_execve`
- `pcb_is_current`
- `get_pid`

它们由 `dispatch_sync()` 根据 syscall number 调用。

## capability lookup

任务 syscall 首先从当前进程 holder 中查找 capability:

- `lookup_pcb(idx, out_cap)` 要求 payload 是 `PCBPayload`。
- `lookup_memory(idx, out_cap)` 要求 payload 是 `MemoryPayload`。
- `current_holder()` 从当前 syscall TCB 找到 `tcb->task->cholder`。

查找失败会返回对应 `ErrCode`。类型不匹配返回 `ErrCode::TYPE_NOT_MATCHED`。

## 创建进程

`pcb_create_process(pcb_cap, image_cap, sched_class, startup)` 对应创建子进程。

流程:

1. lookup `pcb_cap` 并构造 `PCBObject`。
2. 调用 `require_new_process_execute()`，要求 `NEW_PROCESS | EXECUTE`。
3. lookup `image_cap`，要求 payload 类型是 `VFILE` 且 capability 具备 `perm::vfile::EXEC`。
4. 解析用户请求的调度类，只允许 `RT`、`RR`、`FCFS`。
5. 创建子进程 holder。
6. 从父进程 holder 复制用户指定的初始 capability。
7. 在子 holder 中额外插入一个仅带 `EXEC` 权限的镜像文件 capability。
8. 使用配置好的子 holder 调用 `TaskManager::load_elf_into()`。
9. 将子进程 PCB capability 插入当前 holder 的空闲槽。
10. 返回该槽位。

复制初始 capability 时，源 capability 可以走 `CLONE`，也可以走
`MIGRATE` / `MIGRATE_ONCE`。非 shared memory 会额外保护父进程现有映射为 COW。

`pcb_create_linux_process(...)` 与普通创建类似，但最终会走 Linux 子系统装载路径。

## 创建线程

`pcb_create_thread(pcb_cap, entry, stack_addr, stack_size)`:

1. lookup PCB capability。
2. 调用 `PCBObject::require_new_thread()`，要求 `NEW_THREAD`。
3. 要求目标 PCB 是当前进程。
4. 调用 `TaskManager::create_thread_current()`。
5. 返回新 TCB capability 槽位。

线程栈必须已经位于当前进程地址空间中的某个 VMA 内。

## fork

`pcb_fork(pcb_cap, child_cap_buf)`:

1. lookup PCB capability。
2. 调用 `PCBObject::require_new_process()`，要求 `NEW_PROCESS`。
3. 要求目标 PCB 是当前进程。
4. 在当前 holder 中寻找空槽作为子 PCB capability 槽位。
5. 暂时把该槽位写入用户输出缓冲。
6. 调用 `TaskManager::fork_current(child_pcb_cap)`。
7. 成功后 commit 用户缓冲区。
8. 返回子进程 pid。

如果 fork 失败，会用 guard 恢复用户输出缓冲中的原值。

## execve

`pcb_execve(pcb_cap, image_cap, reserved_buf, reserved_sz, startup_buf, startup_buf_sz)`:

1. lookup PCB capability。
2. 调用 `PCBObject::require_execute()`，要求 `EXECUTE`。
3. lookup `image_cap`，要求 payload 类型是 `VFILE` 且 capability 具备 `perm::vfile::EXEC`。
4. 从用户缓冲区读取 reserved cap 列表。
5. 可选读取 startup buffer。
6. 调用 `TaskManager::exec_pcb()` 替换目标进程镜像。

reserved cap 列表决定 exec 后保留哪些 capability。PCB capability 自身也必须保留。

## kill

`pcb_kill(pcb_cap, exit_code)`:

1. lookup PCB capability。
2. 构造 `PCBObject`。
3. 调用 `PCBObject::kill(exit_code)`。

权限检查由 `PCBObject::kill()` 完成，需要 `perm::pcb::KILL`。

kill 会清空目标 holder 并把线程标记为 `DYING`，最后由 PCB payload 释放触发延迟回收。

## map

`pcb_map(pcb_cap, mem_cap, offset, vaddr, sz, protflg)`:

1. lookup PCB capability。
2. lookup Memory capability。
3. 构造 `PCBObject` 和 `MemoryObject`。
4. 调用 `PCBObject::map(memory, offset, vaddr, sz, protflg)`。

权限分两层:

- PCB capability 需要 `VMCONTEXT`。
- Memory capability 需要 `MAP`、`READ`，以及按请求权限需要 `WRITE` / `EXEC` / `FLEXUP` / `FLEXDOWN`。

当前还补充了:

- `pcb_unmap(pcb_cap, vaddr, sz)`
- `pcb_query_vaddr(...)`
- `pcb_query_vspace(...)`

## getpid

`get_pid(pcb_cap)`:

1. 从当前 holder lookup capability。
2. 要求 payload 类型是 `PCB`。
3. 构造 `PCBObject`。
4. 调用 `get_pid()`。

需要 `perm::pcb::GETPID`。

## 调度类参数

用户可请求的调度类仅包括:

- `RT`
- `RR`
- `FCFS`

以下类别会被拒绝:

- `INIT`
- `IDLE`
- `BOT`

## 当前限制

任务 syscall 当前限制如下:

- 创建线程只能作用于当前进程。
- 创建线程不自动分配用户栈。
- 程序镜像必须先由 VFS 打开成 `EXEC` 文件 capability，再传给 create_process / execve。
- `pcb_create_process` 的失败清理仍有 TODO 注释。
- fork 输出 child cap slot 通过用户缓冲区返回，接口语义较底层。
