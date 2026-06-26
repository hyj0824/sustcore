# Memory Syscalls

本文总结 `kernel/syscall/memory.*` 以及它们与任务地址空间、PCB capability 的关系。Memory syscall 操作当前进程的 `CHolder` 和 `TaskMemoryManager`，并通过 `MemoryObject` 做权限检查。

## 当前接口

Memory syscall helper 包括:

- `mem_create`
- `mem_unmap`
- `mem_resize`
- `mem_query`

把 Memory 映射到目标进程的入口在 task syscall 中:

- `pcb_map`

## 创建 Memory

`mem_create(file_cap, memsz, shared, continuity, growth, file_offset)` 会:

1. 如果 `shared == true` 但 growth 不是 `FIXED`，返回 `ErrCode::INVALID_PARAM`。
2. 如果 `file_cap` 有效，则要求它是当前 holder 中一个带 `READ` 权限的
   `VFILE` capability，并为该 backing file clone 一份只读 capability。
3. 创建 `cap::MemoryPayload`。
4. 将 payload 插入当前进程 holder 空闲槽。
5. 返回 capability 槽位。

新 Memory 不会立即分配物理页。物理页由 `MemoryPayload::ensure_page()` 懒分配。
若提供了 `file_cap`，则 Memory 会成为 file-backed memory。

## 映射 Memory

映射入口是:

```cpp
pcb_map(pcb_cap, mem_cap, offset, vaddr, sz, protflg)
```

它通过目标 PCB capability 把 Memory capability 的一段区间映射到某个进程地址空间。

权限规则:

- `PCBObject::map()` 要求 PCB capability 有 `perm::pcb::VMCONTEXT`。
- `MemoryObject::map_into()` 要求 Memory capability 有 `perm::memory::MAP`。
- 映射页必须可读，因此需要 `perm::memory::READ`。
- 请求写权限需要 `perm::memory::WRITE`。
- 请求执行权限需要 `perm::memory::EXEC`。
- 请求向上增长/收缩需要 `perm::memory::FLEXUP`。
- 请求向下增长/收缩需要 `perm::memory::FLEXDOWN`。

映射成功后，目标 TMM 中会创建一个绑定该 Memory payload 的 VMA。

## 取消映射

`mem_unmap(idx, vaddr)`:

1. lookup 当前 holder 中的 Memory capability。
2. 取得当前 PCB 和 TMM。
3. 构造 `MemoryObject`。
4. 调用 `unmap_from(*tmm, vaddr)`。

`MemoryObject::unmap_from()` 要求 `perm::memory::MAP`，然后按 memory 和 vaddr 定位 VMA 并移除。

移除 VMA 会解除页表映射，但不释放 Memory payload 持有的物理页。物理页由 payload 生命周期管理。

## resize

`mem_resize(idx, newsz)`:

1. lookup Memory capability。
2. 取得当前 TMM。
3. 构造 `MemoryObject`。
4. 调用 `resize_in(tmm, newsz)`。

`resize_in()` 要求 `perm::memory::RESIZE`。

行为:

- 缩小时，先让 TMM 解除超出新大小的映射。
- 调整 `MemoryPayload::memsz`。
- 对关联 VMA 调用 `sync_memory_vmas()` 同步范围。

shared memory 当前仍不支持 resize。

## query

`mem_query(idx, out_buf)`:

1. lookup Memory capability。
2. 构造 `MemoryObject`。
3. 调用 `query()`。
4. 将 `MemQueryRet{memsz, allocated}` 写入用户缓冲区。
5. commit 到用户空间。

需要 `perm::memory::QUERY`。

## 与缺页处理的关系

Memory syscall 只创建 payload 和 VMA，不直接建立完整页表映射。

当用户访问映射区域时:

1. RISC-V trap 识别缺页。
2. trap handler 调用当前 TMM 的 `on_np()`。
3. TMM 定位 VMA。
4. VMA 关联的 Memory payload 懒分配物理页。
5. 页表建立映射。

因此 Memory capability 是“承诺内存 + 权限 + VMA 关系”，物理内存按需兑现。

## 与 fork/COW 的关系

fork 时:

- shared Memory payload 在 clone 时返回自身。
- 非 shared Memory payload 会创建克隆 payload。
- 已分配页进入 COW 共享状态。
- 父子页表都对可写页去写权限并设置 COW 标记。

写 COW 页时，写保护异常会调用 `TaskMemoryManager::on_wp()`，最终由 `MemoryPayload::fork()` 拆分物理页。

## 当前限制

当前 Memory syscall 限制如下:

- `UBuffer` 输出要求用户缓冲区落在单个 VMA 中。
- shared memory 不支持 resize。
- `shared + file-backed` 当前不支持。
- COW 不支持大页。
- `mem_unmap` 只作用于当前进程 TMM。
