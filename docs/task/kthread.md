# Kernel Threads

本文总结 Sustcore 的内核进程与内核线程。内核线程仍然使用 `TCB` 和调度器运行，但它们属于唯一内核 PCB，使用内核页表和 S-Mode 上下文。

## 内核 PCB

`TaskManager::create_kernel_task()` 创建唯一内核进程。

主要行为:

1. 如果 `_kernel_pcb` 已存在，直接返回。
2. 分配并初始化 PCB。
3. 设置 `pcb->is_kernel = true`。
4. 通过 `TaskMemoryManager::from_existing_pgd(env::inst().main_kernel_pgd())` 绑定主内核页表。
5. 不创建 capability holder。
6. 将 entrypoint 设置为 `kthread_idle`。
7. 加入 `_pid_map`。

内核 PCB 的特点:

- `cholder == nullptr`
- `pcb_cap == cap::null`
- `main_tcb_cap == cap::null`
- 所有内核线程共享主内核页表。

## 创建内核线程

接口有两个:

```cpp
Result<nonnull<TCB *>> create_kernel_thread(void (*entry)(),
                                            schd::ClassType schd_class);

Result<nonnull<TCB *>> create_kernel_thread(KThreadEntry entry, void *arg,
                                            schd::ClassType schd_class);
```

无参数入口会通过 `kthread_noarg_entry()` 适配成 `KThreadEntry`。

创建规则:

- entry 不能为空。
- 调度类不能是 `INIT` 或 `BOT`。
- 线程必须挂入内核 PCB。
- `kernel_thread = true`，因此上下文以 S-Mode 配置。

创建时实际线程入口是 `kthread_trampoline`。它会从当前 TCB 读取 `kentry/karg`，调用真正入口，入口返回后进入 `kthread_exit()`。

## 内核线程上下文

`construct_thread()` 对内核线程调用:

```cpp
init_ctx(tcb, &kthread_trampoline, nullptr, true);
```

随后 `create_kernel_thread()` 会设置:

```cpp
tcb->context()->sp() = reinterpret_cast<umb_t>(tcb->context());
```

这让内核线程从保存于内核栈上的上下文恢复，并在 S-Mode 执行。

## idle 线程

`create_idle_thread()` 创建特殊 IDLE 内核线程:

```cpp
create_kernel_thread(&kthread_idle, schd::ClassType::IDLE)
```

`kthread_idle()` 是无限循环:

```cpp
while (true) {
    Riscv64Idle::idle();
}
```

调度器初始化时需要传入 idle TCB，并把它设为当前运行线程。没有其它可运行线程时，调度器会回到 IDLE。

## 内核线程退出

`kthread_exit()` 的语义:

1. 获取当前 TCB。
2. 要求当前线程是内核线程。
3. 将线程状态设为 `WAITING`。
4. 设置 `FLAGS_NEED_RESCHED`。
5. 调用 `Scheduler::schedule()`。
6. 如果退出后仍被调度，触发 panic。

当前内核线程没有 join 或返回值语义。

## 调度切换

内核线程和用户线程的切换在 `Scheduler::schedule()` 中区分:

- 内核线程切到内核线程: 调用 `riscv64_kernel_switch(prev_ctx, next_ctx)`。
- 内核线程切到用户线程: 调用 `riscv64_kernel_switch_to_user(prev_ctx, next_kstack)`。
- 用户态返回 trap 后，由 trap 出口和 `isr_restore_user()` 恢复用户上下文。

`run_current()` 会先调度一次，然后:

- 当前是内核线程时调用 `isr_restore_kernel(kstack_bottom)`。
- 当前是用户线程时调用 `isr_restore_user(kstack_bottom)`。

## 与 capability 系统的关系

内核 PCB 没有 holder，因此内核线程不通过 PCB/TCB capability 暴露自己。它们也不能走用户态 `create_thread_current()` 路径。

内核线程如果操作 capability 对象，通常是作为内核内部逻辑直接持有对象指针；对象方法中如果需要“当前线程”，当前实现通常直接退化到 `Scheduler::current_tcb()`。

## 当前限制

当前内核线程系统仍有一些限制:

- 没有内核线程 join、取消、返回值接口。
- 内核线程不能使用 `INIT` 调度类。
- 内核线程的生命周期主要依赖入口函数返回后的 `kthread_exit()`。
- 内核 PCB 不持有 capability holder，因此不能直接复用用户进程 capability 空间语义。
