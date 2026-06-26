# Syscall Dispatch

本文总结 `kernel/syscall/syscall.*` 与 RISC-V trap 入口中 syscall 分发流程。该路径连接用户态寄存器参数、TCB syscall 生命周期、调度器返回值提交和 capability 对象操作。

## 入口

用户态通过 RISC-V `ecall` 进入 trap。当前入口已经拆成两层:

1. `kernel/arch/riscv64/int/exception.cpp` 中的 `on_ecall_u()`
2. `kernel/syscall/syscall.cpp` 中的 `handle_user_ecall(...)`

其中:

- `on_ecall_u()` 只保留架构相关动作
- `handle_user_ecall(...)` 负责通用 syscall 生命周期

`on_ecall_u()` 负责:

1. 将当前 trap context 保存到 `env::inst().trap_context()`
2. 读取当前调度线程 `Scheduler::current_tcb()`
3. 调用 `Riscv64Context::read_args()` 读取参数
4. `ctx->sepc += 4`，跳过 `ecall` 指令
5. 调用 `syscall::handle_user_ecall(...)`

参数寄存器约定:

- `a7`: syscall number。
- `a0`: capidx。
- `a1` 到 `a6`: 普通参数。

返回寄存器约定:

- `a0`: 返回值或 bool。
- `a1`: `ErrCode`。

## 返回包

syscall 返回值统一使用:

```cpp
struct RetPack {
    bool processed;
    b64 ret0;
    b64 ret1;
};
```

辅助函数会把 `Result<T>` 转成 `RetPack`:

- `result_void_ret()`
- `result_value_ret()`
- `result_bool_ret()`

失败时 `ret1` 保存错误码，成功时 `ret1 = ErrCode::SUCCESS`。

## 同步分发

`dispatch_sync(tcb, trap_context, args)` 处理当前主路径上的 syscall。

主要流程:

1. 检查 `tcb->task` 有效。
2. 拆出 syscall number、capidx 和六个参数。
3. switch 分发到具体 syscall helper。
4. 返回 `RetPack`。

同步分发返回后，trap handler 会调用:

```cpp
current_tcb->syscall_info.complete(ret);
```

实际写回用户寄存器由调度器稍后提交。

## 当前等待相关 syscall

等待子系统中的 `FutureAwaiter` / `wait::cotask` 仍然存在，并被对象层和测试代码使用；但当前 `kernel/syscall/syscall.cpp` 的主分发路径是同步的，文档中不再把 `dispatch_async()` 视为主入口。

## 调度器提交 syscall

调度器负责在合适时间提交 syscall 返回值:

- `Scheduler::schedule()` 会处理当前线程已完成 syscall。
- `Scheduler::can_schedule_tcb()` 会处理候选线程已完成 syscall。

提交动作:

```cpp
tcb->context()->write_ret(tcb->syscall_info.syscall_result);
tcb->syscall_info.reset();
```

如果线程仍处于 `EXECUTING`，调度器不会把它恢复到用户态。

## 用户缓冲区

syscall 中的用户指针通过 `UBuffer` / `UString` 访问。

`UBuffer` 行为:

1. 根据 active context 找到当前 TMM。
2. 定位用户地址所在 VMA。
3. 要求整个缓冲区落在同一个 VMA 中。
4. 找到对应 `MemoryPayload` 和偏移。
5. `sync_from_user()` 用 payload read 复制到内核缓冲。
6. `commit_to_user()` 用 payload write 写回用户空间。

这避免 syscall 层直接解引用用户虚拟地址。

## 当前限制

当前 syscall 分发仍有一些限制:

- `UBuffer` 不支持跨 VMA 缓冲区。
- 未知 syscall 返回 `ErrCode::NOT_SUPPORTED`。
