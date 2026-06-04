# Memory Object

本文总结 `kernel/object/memory.*`。它描述了当前系统中的 Memory capability: 它如何表示一段逻辑内存、如何按需分配物理页、如何支持 COW，以及如何映射到进程地址空间。

## 总体模型

Memory capability 不是“已经映射好的虚拟内存区”，而是:

- 一段承诺大小 `memsz`
- 一组按需分配的物理页
- 一套增长/收缩约束
- 一组权限位

VMA 只是引用 `MemoryPayload`，实际物理页生命周期由 payload 自己管理。

## `MemoryPayload`

`MemoryPayload` 是 `PayloadType::MEMORY` 的实现。

它包含:

- `memsz`: 逻辑大小
- `shared`: clone 时是否共享 payload
- `continuity`: 是否要求物理连续
- `growth`: 允许的扩缩容方向
- `phy_pages`: `offvpn -> PhyPage`

### `PhyPage`

每个已分配页记录:

- `PhyAddr addr`
- `size_t refcount`

这里的 `refcount` 不是 payload 引用计数，而是该页在当前 COW 语义下的共享计数。

## 逻辑大小与实际分配

Memory 的核心是“承诺大小”和“实际分配”分离。

- `memsz` 决定理论可访问区间
- `phy_pages` 只记录已经真正分配过的页

因此:

- 一个大 `memsz` 的 Memory 不会一次性占满物理页
- 访问未分配页时会触发懒分配

## clone 语义

`MemoryPayload::clone_payload()` 分两种情况。

### `shared == true`

直接返回 `this`，多个 capability 共享同一个 payload。

### `shared == false`

会创建一个新的 `MemoryPayload`，然后:

1. 对每个已分配页调用 `GFP::keep_page`
2. 增加页记录里的 `refcount`
3. 把页记录复制进新 payload

这意味着:

- payload 本身被复制
- 物理页初始共享
- 后续写入时再通过 `fork()` 拆分

这就是当前 Memory capability 的 COW 机制。

## 页级辅助语义

内部使用几个关键概念:

- `offset -> offvpn`
- `offvpn -> page-aligned offset`
- `offset_in_page`

这些辅助函数把“逻辑内偏移”映射到 payload 内的页编号。

## 懒分配

### `lookup_page(offset)`

查询指定偏移所在页:

- 越界返回 `OUT_OF_BOUNDARY`
- 未分配返回 `PAGE_NOT_PRESENT`
- 已分配返回物理页地址

### `ensure_page(offset)`

若目标页尚未存在，则:

1. `GFP::get_free_page(1)`
2. 清零整页
3. 放入 `phy_pages`

之后返回物理页地址。

因此 Memory 默认按“首次访问分配零页”的方式工作。

## 读写语义

### `read(offset, buf, len)`

读取流程是:

1. 计算实际可读长度 `min(memsz - offset, len)`
2. 按页分块
3. 对每页调用 `ensure_page()`
4. 从物理页拷贝到输出缓冲区

注意这里读未分配页也会触发实际分配零页，而不是直接返回逻辑零值。

### `write(offset, buf, len)`

写流程是:

1. 计算实际可写长度
2. 按页分块
3. `ensure_page()`
4. 检查页级 `refcount`
5. 若 `refcount > 1`，先 `fork(offset)`
6. 再写入物理页

因此写入路径天然带有 COW 拆分逻辑。

## `fork(offset)` 的含义

`MemoryPayload::fork(offset)` 只针对单页执行写时复制拆分。

### `refcount <= 1`

说明该页已经是当前 payload 独占，不需要复制。

### `refcount > 1`

则:

1. 分配新页
2. 把旧页内容复制到新页
3. 当前 payload 的该页记录替换成新页
4. 当前 payload 视角下 `refcount = 1`
5. 对旧页调用 `GFP::put_page`

因此 fork 的目标不是复制整个 Memory，而是只在写路径拆出当前页。

## resize 语义

`MemoryPayload::resize(newsz)` 的实现受两个条件控制:

1. `shared` 不能为 true
2. `growth` 必须允许对应 grow/shrink 动作

### 可变方向

`MemoryGrowth` 包括:

- `FIXED`
- `GROW_UP`
- `GROW_DOWN`
- `SHRINK_UP`
- `SHRINK_DOWN`
- `FLEXUP`
- `FLEXDOWN`

实际检查逻辑是:

- 变大时需要允许 `GROW_*`
- 变小时需要允许 `SHRINK_*`

### 非连续内存

若 `continuity == false`:

- 扩容只修改 `memsz`
- 缩容时调用 `release_pages_from(newsz)` 释放尾部页

### 连续内存

若 `continuity == true`:

1. 根据新大小重新申请一段连续物理页
2. 把旧页内容复制过去
3. 释放旧页
4. 重建整套 `phy_pages`

所以连续内存 resize 的代价明显更高。

## 映射到进程地址空间

对象操作层由 `MemoryObject` 提供。

### `map_into(tmm, vaddr, rwx, growth)`

它要求:

- `perm::memory::MAP`
- 至少有 `READ`
- 若 `rwx` 可写，则还要有 `WRITE`
- 若 `rwx` 可执行，则还要有 `EXEC`
- 若请求上向增长/收缩，则还要有 `FLEXUP`
- 若请求下向增长/收缩，则还要有 `FLEXDOWN`

此外，还要求请求的 `growth` 不超出 payload 自身允许的增长模式。

映射前若 `continuity == true` 且尚未分配物理页，还会先调用 `ensure_contiguous()` 预分配整段连续物理页。

最后通过:

```cpp
tmm.add_vma(VMA::Type::SHARE_RW, growth, area, _obj, rwx)
```

把该 Memory 挂进目标任务的 VMA 集合。

### `unmap_from(tmm, vaddr)`

要求 `MAP` 权限。

它会先定位该 `MemoryPayload` 在目标地址上的 VMA，再调用 `remove_vma()`。

### `resize_in(tmm, newsz)`

要求 `RESIZE` 权限。

行为:

1. 若缩小并且传入了 `tmm`，先 `unmap_memory_tail()`
2. 调用 payload `resize(newsz)`
3. 若传入了 `tmm`，再 `sync_memory_vmas(_obj)`

因此对象 resize 不只是改 payload 大小，还会同步映射关系。

### `query()`

要求 `QUERY` 权限。

返回:

- `memsz`
- `allocated_size()`

## 权限模型

定义在 `perm::memory`:

- `MAP`
- `READ`
- `WRITE`
- `EXEC`
- `RESIZE`
- `QUERY`
- `FLEXUP`
- `FLEXDOWN`

这些权限分工比较清晰:

- 访问权限控制页权限上界
- 结构权限控制映射、扩缩容和可变增长方向

## 与 clone/能力系统的关系

Memory capability 是能力系统里最特殊的一类对象之一:

- capability clone 不只是 capability 层面的复制
- payload clone 会复制元数据并共享物理页
- `CHolder::clone()` 还会调用 `tmm->protect_memory_cow(memory)`

这意味着 Memory 的 clone 语义横跨:

1. capability 层
2. payload 层
3. 当前任务页表层

## 当前设计特点

这个 Memory 对象的设计要点是:

- **逻辑大小与物理分配解耦**
- **缺页式懒分配**
- **clone 后按页 COW**
- **连续物理内存单独建模**
- **映射能力与对象能力分离**

它本质上把“共享内存对象”和“可映射内存能力”统一成了一种 capability payload。
